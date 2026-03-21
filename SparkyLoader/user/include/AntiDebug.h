#pragma once
// ---------------------------------------------------------------------------
// AntiDebug.h — layered debugger detection, server self-report, and
//               binary self-deletion.
//
// On detection the loader:
//   1. Scrubs all sensitive in-memory buffers (HWID, keys, DLL bytes).
//   2. POSTs /api/tamper-report to the server with HWID + HMAC proof so the
//      server can ban the HWID and the source IP.
//   3. Renames the binary and schedules OS-level deletion so no copy remains.
//   4. Calls TerminateProcess(GetCurrentProcess(), 0) — immediate, no cleanup.
//
// Detection methods (all run independently):
//   A. IsDebuggerPresent()              — PEB.BeingDebugged flag
//   B. CheckRemoteDebuggerPresent()     — remote debug port
//   C. NtQueryInformationProcess/0x07   — ProcessDebugPort
//   D. NtQueryInformationProcess/0x1E   — ProcessDebugObjectHandle
//   E. GetThreadContext hardware bps    — Dr0-Dr3, Dr7 non-zero
//   F. PEB.NtGlobalFlag heap bits       — 0x70 flags set by WinDbg/CDB
//
// All NT function names and DLL names are obfuscated via _S() so they do
// not appear as plaintext strings in the .rdata section.
// ---------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include "StringCrypt.h"
#include "TlsLayer.h"    // NetSend

// NtQueryInformationProcess signature — resolved dynamically.
using NtQIP_fn = LONG (NTAPI*)(HANDLE Process,
                                UINT   InfoClass,
                                PVOID  Info,
                                ULONG  InfoLen,
                                PULONG RetLen);

namespace AntiDebug
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static NtQIP_fn GetNtQIP()
{
    SPARKY_STR(ntdll_s,  "ntdll.dll");
    SPARKY_STR(fname_s,  "NtQueryInformationProcess");
    HMODULE h = GetModuleHandleA(ntdll_s.c_str());
    if (!h) return nullptr;
    return reinterpret_cast<NtQIP_fn>(GetProcAddress(h, fname_s.c_str()));
}

// Encode bytes as lowercase hex into a caller-supplied buffer (must be 2*n+1 bytes).
static void ToHex(const uint8_t* in, int n, char* out)
{
    static const char kH[] = "0123456789abcdef";
    for (int i = 0; i < n; ++i)
    {
        out[i * 2]     = kH[in[i] >> 4];
        out[i * 2 + 1] = kH[in[i] & 0xF];
    }
    out[n * 2] = '\0';
}

// ---------------------------------------------------------------------------
// CheckAll — returns true if ANY method detects a debugger.
// Call from a tight loop for continuous monitoring.
// ---------------------------------------------------------------------------
static bool CheckAll()
{
    // ── A. IsDebuggerPresent (PEB.BeingDebugged) ─────────────────────────
    if (IsDebuggerPresent())
        return true;

    // ── B. CheckRemoteDebuggerPresent ────────────────────────────────────
    {
        BOOL present = FALSE;
        CheckRemoteDebuggerPresent(GetCurrentProcess(), &present);
        if (present) return true;
    }

    // ── C. NtQueryInformationProcess — ProcessDebugPort (0x07) ───────────
    // Returns a non-null debug port handle when the process is under a
    // kernel-mode debugger.  Harder to bypass than IsDebuggerPresent().
    {
        static NtQIP_fn ntQIP = GetNtQIP();
        if (ntQIP)
        {
            HANDLE port = nullptr;
            if (ntQIP(GetCurrentProcess(), 0x07,
                      &port, sizeof(port), nullptr) >= 0 && port)
                return true;
        }
    }

    // ── D. NtQueryInformationProcess — ProcessDebugObjectHandle (0x1E) ───
    // Returns a handle to the debug object; non-null → being debugged.
    {
        static NtQIP_fn ntQIP = GetNtQIP();
        if (ntQIP)
        {
            HANDLE obj = nullptr;
            LONG r = ntQIP(GetCurrentProcess(), 0x1E,
                           &obj, sizeof(obj), nullptr);
            // STATUS_PORT_NOT_SET (0xC0000353) → not debugged.
            // Any other success with a non-null handle → debugged.
            if (r >= 0 && obj)
            {
                CloseHandle(obj);
                return true;
            }
        }
    }

    // ── E. Hardware breakpoints via GetThreadContext ───────────────────
    // x64dbg, OllyDbg, and Cheat Engine set debug registers to trap
    // specific addresses.  A legitimate user has all debug regs = 0.
    {
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(GetCurrentThread(), &ctx))
        {
            // Dr0-Dr3 hold breakpoint addresses; Dr7 is the control register.
            // Low byte of Dr7 enables individual BPs — any non-zero = active BP.
            if (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3 ||
                (ctx.Dr7 & 0xFF))
                return true;
        }
    }

    // ── F. PEB.NtGlobalFlag heap flags ───────────────────────────────────
    // WinDbg, CDB, and most debuggers set FLG_HEAP_ENABLE_TAIL_CHECK |
    // FLG_HEAP_ENABLE_FREE_CHECK | FLG_HEAP_VALIDATE_PARAMETERS (0x70)
    // in the NtGlobalFlag field of the Process Environment Block.
    {
#ifdef _WIN64
        const BYTE* peb        = (const BYTE*)__readgsqword(0x60);
        const DWORD ntGlobalFlag = *reinterpret_cast<const DWORD*>(peb + 0xBC);
#else
        const BYTE* peb        = (const BYTE*)__readfsdword(0x30);
        const DWORD ntGlobalFlag = *reinterpret_cast<const DWORD*>(peb + 0x68);
#endif
        if (ntGlobalFlag & 0x70u)
            return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// SelfReport — HTTP POST /api/tamper-report to the server.
//
// Body: { "hwid": "<64 hex>", "hmac": "<64 hex>" }
// where hmac = HMAC-SHA256(hwid_bytes, attest_key).
//
// The server verifies the HMAC and then bans the HWID + the source IP.
// Uses a 3-second hard timeout — fire-and-forget before self-deletion.
// ---------------------------------------------------------------------------
static void SelfReport(const char*    host,
                       int            port,
                       SSL_CTX*       sslCtx,   // may be nullptr (plaintext fallback)
                       const uint8_t  hwid[32],
                       const uint8_t  attestKey[32],
                       bool           hasAttestKey)
{
    // Compute HMAC-SHA256(hwid, attest_key) as the authenticity proof.
    uint8_t hmac[32]{};
    if (hasAttestKey)
    {
        unsigned hmacLen = 32;
        HMAC(EVP_sha256(), attestKey, 32, hwid, 32, hmac, &hmacLen);
    }
    else
    {
        // No key available — send raw hwid (server will still ban the IP).
        memcpy(hmac, hwid, 32);
    }

    char hwidHex[65]{}, hmacHex[65]{};
    ToHex(hwid, 32, hwidHex);
    ToHex(hmac, 32, hmacHex);
    SecureZeroMemory(hmac, sizeof(hmac));

    // Build JSON body.
    char body[256]{};
    snprintf(body, sizeof(body),
             "{\"hwid\":\"%s\",\"hmac\":\"%s\"}", hwidHex, hmacHex);
    SecureZeroMemory(hwidHex, sizeof(hwidHex));
    SecureZeroMemory(hmacHex, sizeof(hmacHex));

    // ── Open connection ──────────────────────────────────────────────────
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    char portStr[8]{};
    snprintf(portStr, sizeof(portStr), "%d", port);

    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    SOCKET sock = INVALID_SOCKET;
    SSL*   ssl  = nullptr;

    if (getaddrinfo(host, portStr, &hints, &res) == 0 && res)
    {
        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock != INVALID_SOCKET)
        {
            // Hard 3-second timeout — we cannot wait longer before dying.
            DWORD to = 3000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&to, sizeof(to));

            if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == 0)
            {
                if (sslCtx)
                {
                    ssl = SSL_new(sslCtx);
                    SSL_set_fd(ssl, (int)sock);
                    SSL_set_tlsext_host_name(ssl, host);
                    if (SSL_connect(ssl) <= 0)
                    {
                        SSL_free(ssl);
                        ssl = nullptr;
                    }
                }

                // Build and send the HTTP POST.
                char req[1024]{};
                SPARKY_STR(endpoint, "/api/tamper-report");
                SPARKY_STR(ct,       "application/json");
                snprintf(req, sizeof(req),
                    "POST %s HTTP/1.1\r\n"
                    "Host: %s\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Length: %d\r\n"
                    "Connection: close\r\n"
                    "\r\n%s",
                    endpoint.c_str(), host, ct.c_str(),
                    (int)strlen(body), body);

                NetSend(sock, ssl, req, (int)strlen(req));
                // No need to read the response — we are about to exit.
                SecureZeroMemory(req, sizeof(req));
            }
        }
        freeaddrinfo(res);
    }

    if (ssl)  { SSL_shutdown(ssl); SSL_free(ssl); }
    if (sock != INVALID_SOCKET) closesocket(sock);
    WSACleanup();

    SecureZeroMemory(body, sizeof(body));
}

// ---------------------------------------------------------------------------
// SelfDelete — remove the loader binary from disk.
//
// Running EXEs cannot be deleted directly on Windows because the OS holds a
// file lock for the image mapping.  Strategy:
//   1. Rename the EXE to a hidden temp path (makes it invisible to the user).
//   2. Spawn a hidden cmd.exe that waits ~2 s (for this process to exit) and
//      then force-deletes the renamed file.
//   3. Fallback: if rename fails, mark the original path for deletion on
//      the next reboot via MoveFileExW(MOVEFILE_DELAY_UNTIL_REBOOT).
// ---------------------------------------------------------------------------
static void SelfDelete()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    wchar_t tmpDir[MAX_PATH]{}, tmpPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmpDir);
    // GetTempFileName creates the placeholder file — we must remove it first.
    GetTempFileNameW(tmpDir, L"~sk", 0, tmpPath);
    DeleteFileW(tmpPath);

    if (MoveFileW(path, tmpPath))
    {
        // Build a minimal cmd.exe command to delete the renamed file after exit.
        // ping 127.0.0.1 -n 2 gives ~2 seconds of delay (1 ping + 1 wait).
        wchar_t cmd[1024]{};
        swprintf_s(cmd, 1024,
            L"cmd.exe /C ping 127.0.0.1 -n 2 >nul && del /F /Q \"%s\"",
            tmpPath);

        STARTUPINFOW si{ sizeof(si) };
        si.dwFlags     = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        CreateProcessW(nullptr, cmd,
                       nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | DETACHED_PROCESS,
                       nullptr, nullptr, &si, &pi);
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread)  CloseHandle(pi.hThread);
    }
    else
    {
        // Rename failed (locked or permissions) — schedule deletion on reboot.
        MoveFileExW(path, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
}

} // namespace AntiDebug
