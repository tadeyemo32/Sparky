// SparkyLoader — main entry point
// GUI + server auth + in-memory DLL receive + stealth inject
#include <Windows.h>
#include <TlHelp32.h>
#include <wincrypt.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "psapi.lib")

#include "Logger.h"
#include "ManualMap.h"
#include "UI.h"
#include "Protocol.h"

// ---------------------------------------------------------------------------
// HWID: SHA-256 of MachineGuid registry key via CryptAPI.
// ---------------------------------------------------------------------------
static bool GetHwidHash(uint8_t out[32])
{
    HKEY hKey{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    wchar_t guid[64]{};
    DWORD cb = sizeof(guid), type = REG_SZ;
    bool ok = RegQueryValueExW(hKey, L"MachineGuid", nullptr, &type,
                               reinterpret_cast<LPBYTE>(guid), &cb) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    if (!ok) return false;

    HCRYPTPROV hProv{};
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return false;

    HCRYPTHASH hHash{};
    ok = false;
    if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
    {
        if (CryptHashData(hHash, reinterpret_cast<BYTE*>(guid), cb - (DWORD)sizeof(wchar_t), 0))
        {
            DWORD hl = 32;
            ok = CryptGetHashParam(hHash, HP_HASHVAL, out, &hl, 0) == TRUE;
        }
        CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);
    return ok;
}

// ---------------------------------------------------------------------------
// GetLoaderHash: SHA-256 of the loader binary itself.
// Sent to the server for integrity checking (trusted_hashes table).
// ---------------------------------------------------------------------------
static bool GetLoaderHash(uint8_t out[32])
{
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;

    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    HCRYPTPROV hProv{};
    HCRYPTHASH hHash{};
    bool ok = false;

    if (CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)
        && CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
    {
        uint8_t buf[65536];
        DWORD bytesRead = 0;
        ok = true;
        while (ReadFile(hFile, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0)
        {
            if (!CryptHashData(hHash, buf, bytesRead, 0)) { ok = false; break; }
        }
        if (ok)
        {
            DWORD hl = 32;
            ok = CryptGetHashParam(hHash, HP_HASHVAL, out, &hl, 0) == TRUE;
        }
        CryptDestroyHash(hHash);
    }
    if (hProv) CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return ok;
}

// ---------------------------------------------------------------------------
// CryptRandBytes — generate N random bytes via CryptGenRandom.
// Used for heartbeat nonces.
// ---------------------------------------------------------------------------
static bool CryptRandBytes(uint8_t* out, DWORD n)
{
    HCRYPTPROV hp{};
    if (!CryptAcquireContextW(&hp, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return false;
    bool ok = CryptGenRandom(hp, n, out) == TRUE;
    CryptReleaseContext(hp, 0);
    return ok;
}

// ---------------------------------------------------------------------------
// FindProcessByName — Toolhelp32 snapshot, no LoadLibrary.
// ---------------------------------------------------------------------------
static DWORD FindProcessByName(const char* name)
{
    HANDLE hSn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSn == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32A pe{ sizeof(pe) };
    DWORD pid = 0;
    if (Process32FirstA(hSn, &pe))
        do { if (_stricmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; } }
        while (Process32NextA(hSn, &pe));
    CloseHandle(hSn);
    return pid;
}

// ---------------------------------------------------------------------------
// Raw socket helpers (avoid C++ iostream which may call LoadLibraryExW)
// ---------------------------------------------------------------------------
static bool RawSend(SOCKET s, const void* d, int n)
{
    const char* p = (const char*)d; int sent = 0;
    while (sent < n) { int r = send(s, p+sent, n-sent, 0); if (r <= 0) return false; sent += r; }
    return true;
}
static bool RawRecv(SOCKET s, void* d, int n, DWORD ms = 10000)
{
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&ms, sizeof(ms));
    char* p = (char*)d; int got = 0;
    while (got < n) { int r = recv(s, p+got, n-got, 0); if (r <= 0) return false; got += r; }
    return true;
}

// ---------------------------------------------------------------------------
// Session state
// ---------------------------------------------------------------------------
struct ServerSession
{
    SOCKET   sock    = INVALID_SOCKET;
    uint64_t hdrKey  = 0; // 0 until AuthOk is processed (AuthOk itself is plain)
    uint64_t dllKey  = 0;
    uint8_t  token[16]{};
};

// SendMsg: encrypts payload with hdrKey if non-zero, computes CRC-32.
static bool SendMsg(ServerSession& ss, MsgType t,
                    const void* pay = nullptr, uint16_t len = 0)
{
    MsgHeader h{};
    h.Magic   = PROTO_MAGIC;
    h.Version = PROTO_VERSION;
    h.Type    = t;
    h.Length  = len;

    std::vector<uint8_t> buf(len);
    if (pay && len) memcpy(buf.data(), pay, len);
    if (ss.hdrKey && !buf.empty())
        XorStream(buf.data(), (uint32_t)buf.size(), ss.hdrKey);

    uint32_t crc = Crc32(reinterpret_cast<uint8_t*>(&h), sizeof(h));
    if (!buf.empty()) crc ^= Crc32(buf.data(), (uint32_t)buf.size());

    return RawSend(ss.sock, &h, sizeof(h))
        && (buf.empty() || RawSend(ss.sock, buf.data(), (int)buf.size()))
        && RawSend(ss.sock, &crc, 4);
}

// RecvMsg: reads header + payload + CRC, verifies integrity, decrypts payload.
static bool RecvMsg(ServerSession& ss, MsgType& t,
                    std::vector<uint8_t>& pay, DWORD ms = 10000)
{
    MsgHeader h{};
    if (!RawRecv(ss.sock, &h, sizeof(h), ms))             return false;
    if (h.Magic != PROTO_MAGIC || h.Version != PROTO_VERSION) return false;

    pay.resize(h.Length);
    if (h.Length && !RawRecv(ss.sock, pay.data(), h.Length, ms)) return false;

    uint32_t rc{};
    if (!RawRecv(ss.sock, &rc, 4, ms)) return false;

    uint32_t lc = Crc32(reinterpret_cast<uint8_t*>(&h), sizeof(h));
    if (!pay.empty()) lc ^= Crc32(pay.data(), (uint32_t)pay.size());
    if (lc != rc) return false; // integrity check failed

    // Decrypt payload (hdrKey == 0 means no encryption yet — used for AuthOk)
    if (ss.hdrKey && !pay.empty())
        XorStream(pay.data(), (uint32_t)pay.size(), ss.hdrKey);

    t = h.Type;
    return true;
}

// ---------------------------------------------------------------------------
// Post-delivery heartbeat thread — keeps the server session alive after the
// DLL has been received.  Takes ownership of the socket.
// Sends a Heartbeat every 25 seconds; stops when the socket is closed.
// ---------------------------------------------------------------------------
struct HeartbeatArgs
{
    SOCKET   sock;
    uint64_t hdrKey;
};

static DWORD WINAPI HeartbeatLoop(LPVOID p)
{
    auto* a = static_cast<HeartbeatArgs*>(p);
    ServerSession ss{};
    ss.sock   = a->sock;
    ss.hdrKey = a->hdrKey;
    delete a;

    while (true)
    {
        Sleep(25000); // every 25 s (server deadline is 35 s)

        HeartbeatPayload hb{};
        // Post-delivery HBs use zero nonce — server just ACKs, no key roll here
        if (!SendMsg(ss, MsgType::Heartbeat, &hb, sizeof(hb))) break;

        MsgType mt{}; std::vector<uint8_t> mp;
        if (!RecvMsg(ss, mt, mp, 10000)) break; // server gone
    }

    closesocket(ss.sock);
    WSACleanup();
    return 0;
}

// ---------------------------------------------------------------------------
// ConnectAndFetchDll
//
// Full protocol flow (must mirror HandleClient on the server exactly):
//
//  1.  Connect TCP
//  2.  Send Hello { HwidHash, BuildId, LoaderHash }   [plain, hdrKey=0]
//  3.  Recv AuthOk or AuthFail                         [plain, hdrKey=0]
//        • AuthFail → log + return {}
//        • AuthOk   → extract token, derive hdrKey + dllKey
//  4.  Recv Config (optional) or BinaryReady           [encrypted]
//  5.  If Config: log, then recv BinaryReady
//  6.  Chunk receive loop:
//        For each batch of br.ChunksPerHeartbeat chunks:
//          Recv BinaryChunk, XorStream with rollingKey, append to buffer
//          After batch: generate nonce, SendMsg(Heartbeat{nonce})
//                        Recv Ack
//                        rollingKey = RollKey(rollingKey, nonce)
//  7.  Recv BinaryEnd
//  8.  Launch HeartbeatLoop background thread (owns socket)
//  9.  Return decrypted DLL bytes
// ---------------------------------------------------------------------------
static std::vector<uint8_t> ConnectAndFetchDll(
    const char* host, int port,
    const uint8_t hwidHash[32],
    const uint8_t loaderHash[32],
    UIState& state)
{
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2,2), &wsa);

    ServerSession ss{};
    ss.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ss.sock == INVALID_SOCKET) { WSACleanup(); return {}; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    DWORD connTo = 5000;
    setsockopt(ss.sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&connTo, sizeof(connTo));

    if (connect(ss.sock, (sockaddr*)&addr, sizeof(addr)) != 0)
    {
        state.AddLog("[ERR] Cannot connect to server");
        closesocket(ss.sock); WSACleanup(); return {};
    }

    // ------------------------------------------------------------------
    // Step 2: Send Hello  (plain — hdrKey == 0, so SendMsg sends raw)
    // ------------------------------------------------------------------
    HelloPayload hello{};
    memcpy(hello.HwidHash,   hwidHash,   32);
    memcpy(hello.LoaderHash, loaderHash, 32);
    hello.BuildId = 0x0001'0000;

    if (!SendMsg(ss, MsgType::Hello, &hello, sizeof(hello)))
    {
        state.AddLog("[ERR] Send Hello failed");
        closesocket(ss.sock); WSACleanup(); return {};
    }

    // ------------------------------------------------------------------
    // Step 3: Receive AuthOk or AuthFail  (PLAIN — hdrKey still 0)
    // AuthOk payload contains the plain-text session token.
    // We must NOT set hdrKey before this receive, or we would try to
    // decrypt a message that was sent unencrypted.
    // ------------------------------------------------------------------
    {
        MsgType t{};
        std::vector<uint8_t> pay;
        if (!RecvMsg(ss, t, pay))
        {
            state.AddLog("[ERR] No response from server");
            closesocket(ss.sock); WSACleanup(); return {};
        }

        if (t == MsgType::AuthFail)
        {
            state.AddLog("[ERR] Auth rejected by server (check license / HWID)");
            closesocket(ss.sock); WSACleanup(); return {};
        }

        if (t != MsgType::AuthOk || pay.size() < sizeof(AuthOkPayload))
        {
            state.AddLog("[ERR] Unexpected message during auth");
            closesocket(ss.sock); WSACleanup(); return {};
        }

        const auto& aok = *reinterpret_cast<const AuthOkPayload*>(pay.data());
        memcpy(ss.token, aok.SessionToken, 16);

        // NOW derive session keys — all messages from here on are encrypted
        ss.hdrKey = DeriveKey(ss.token, 0);
        ss.dllKey = DeriveKey(ss.token, 1);

        state.serverConnected = true;
        state.AddLog("[INF] Authenticated — session keys active");
    }

    // ------------------------------------------------------------------
    // Step 4: Receive optional Config, then BinaryReady
    // (both encrypted with hdrKey)
    // ------------------------------------------------------------------
    BinaryReadyPayload br{};
    {
        MsgType t{};
        std::vector<uint8_t> pay;
        if (!RecvMsg(ss, t, pay))
        {
            state.AddLog("[ERR] No post-auth message received");
            closesocket(ss.sock); WSACleanup(); return {};
        }

        if (t == MsgType::Config)
        {
            state.AddLog("[INF] Config received (" + std::to_string(pay.size()) + " bytes)");

            // Expect BinaryReady next
            if (!RecvMsg(ss, t, pay))
            {
                state.AddLog("[ERR] No BinaryReady after Config");
                closesocket(ss.sock); WSACleanup(); return {};
            }
        }

        if (t != MsgType::BinaryReady || pay.size() < sizeof(BinaryReadyPayload))
        {
            state.AddLog("[ERR] Expected BinaryReady");
            closesocket(ss.sock); WSACleanup(); return {};
        }

        br = *reinterpret_cast<const BinaryReadyPayload*>(pay.data());
        state.AddLog("[INF] DLL incoming: " + std::to_string(br.TotalBytes)
                     + " bytes, " + std::to_string(br.NumChunks)
                     + " chunks, HB every " + std::to_string(br.ChunksPerHeartbeat));
    }

    if (br.TotalBytes == 0 || br.NumChunks == 0 || br.ChunksPerHeartbeat == 0)
    {
        state.AddLog("[ERR] Invalid BinaryReady parameters");
        closesocket(ss.sock); WSACleanup(); return {};
    }

    // ------------------------------------------------------------------
    // Step 6: Receive chunks with rolling-key decryption and heartbeat sync
    //
    // MUST mirror StreamEncryptedDll on the server exactly:
    //   • Receive ChunksPerHeartbeat chunks, decrypt each with rollingKey
    //   • Generate a CryptGenRandom nonce
    //   • Send Heartbeat{nonce}
    //   • Receive Ack
    //   • rollingKey = RollKey(rollingKey, nonce)
    //   • Repeat for remaining batches (last batch may be smaller)
    // ------------------------------------------------------------------
    std::vector<uint8_t> dllBuf;
    dllBuf.reserve(br.TotalBytes);

    uint64_t rollingKey = ss.dllKey; // initial; rolls after each HB batch

    for (uint32_t c = 0; c < br.NumChunks; ++c)
    {
        // Receive one chunk
        MsgType ct{};
        std::vector<uint8_t> cp;
        if (!RecvMsg(ss, ct, cp, 20000) || ct != MsgType::BinaryChunk)
        {
            state.AddLog("[ERR] Expected BinaryChunk at " + std::to_string(c));
            closesocket(ss.sock); WSACleanup(); return {};
        }

        // Decrypt chunk with current rolling key, accumulate
        XorStream(cp.data(), (uint32_t)cp.size(), rollingKey);
        dllBuf.insert(dllBuf.end(), cp.begin(), cp.end());

        // Heartbeat point: after every ChunksPerHeartbeat chunks, AND after
        // the final chunk — must be identical to the server's condition
        if ((c + 1) % br.ChunksPerHeartbeat == 0 || c + 1 == br.NumChunks)
        {
            // Generate a fresh random nonce for this heartbeat
            HeartbeatPayload hb{};
            if (!CryptRandBytes(hb.Nonce, 16))
            {
                // Fallback: xorshift mix of tick count + chunk index
                uint64_t seed = (uint64_t)GetTickCount64() ^ ((uint64_t)c * 0x9E3779B97F4A7C15ULL);
                for (int i = 0; i < 16; i += 8) {
                    seed ^= seed >> 12; seed ^= seed << 25; seed ^= seed >> 27;
                    memcpy(hb.Nonce + i, &seed, 8);
                }
            }

            // Send heartbeat (encrypted with hdrKey)
            if (!SendMsg(ss, MsgType::Heartbeat, &hb, sizeof(hb)))
            {
                state.AddLog("[ERR] Heartbeat send failed at chunk " + std::to_string(c));
                closesocket(ss.sock); WSACleanup(); return {};
            }

            // Wait for Ack (must arrive within HEARTBEAT_DEADLINE_MS)
            MsgType at{}; std::vector<uint8_t> ap;
            if (!RecvMsg(ss, at, ap, HEARTBEAT_DEADLINE_MS) || at != MsgType::Ack)
            {
                state.AddLog("[ERR] No Ack after heartbeat at chunk " + std::to_string(c));
                closesocket(ss.sock); WSACleanup(); return {};
            }

            // Advance rolling key — server does the same with the same nonce
            rollingKey = RollKey(rollingKey, hb.Nonce);
        }
    }

    // ------------------------------------------------------------------
    // Step 7: Receive BinaryEnd
    // ------------------------------------------------------------------
    {
        MsgType et{}; std::vector<uint8_t> ep;
        if (!RecvMsg(ss, et, ep, 10000) || et != MsgType::BinaryEnd)
        {
            state.AddLog("[WRN] Missing BinaryEnd — continuing anyway");
        }
        else
        {
            state.AddLog("[INF] Transfer complete");
        }
    }

    if (dllBuf.size() != br.TotalBytes)
    {
        state.AddLog("[ERR] DLL size mismatch: got " + std::to_string(dllBuf.size())
                     + " expected " + std::to_string(br.TotalBytes));
        closesocket(ss.sock); WSACleanup(); return {};
    }

    state.AddLog("[INF] DLL decrypted in RAM (" + std::to_string(dllBuf.size()) + " bytes)");

    // ------------------------------------------------------------------
    // Step 8: Hand socket to background heartbeat thread.
    // The thread keeps the server session alive until the loader exits.
    // ------------------------------------------------------------------
    auto* hbArgs   = new HeartbeatArgs{ ss.sock, ss.hdrKey };
    ss.sock        = INVALID_SOCKET; // thread now owns the socket
    CreateThread(nullptr, 0, HeartbeatLoop, hbArgs, 0, nullptr);
    // WSACleanup is called by HeartbeatLoop when the socket closes

    return dllBuf;
}

// ---------------------------------------------------------------------------
// ProcessWatcher — background thread, no heap allocs
// ---------------------------------------------------------------------------
static void ProcessWatcher(UIState& state, std::atomic<bool>& running)
{
    while (running.load())
    {
        DWORD pid = FindProcessByName(state.processName);
        state.processFound = (pid != 0);
        state.targetPid    = pid;
        Sleep(1000);
    }
}

// ---------------------------------------------------------------------------
// wWinMain — WIN32 subsystem entry point (no console window)
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    Logger::Init("SparkyLoader.log");

    UIState state{};
    state.AddLog("[INF] Sparky ready");

    uint8_t hwidHash[32]{};
    if (!GetHwidHash(hwidHash))
        state.AddLog("[WRN] HWID hash failed");

    uint8_t loaderHash[32]{};
    if (!GetLoaderHash(loaderHash))
        state.AddLog("[WRN] Loader hash failed — integrity check will reject");

    std::atomic<bool> watcherRunning = true;
    HANDLE hWatcher = CreateThread(nullptr, 0,
        [](LPVOID p) -> DWORD {
            auto* args = (std::pair<UIState*, std::atomic<bool>*>*)p;
            ProcessWatcher(*args->first, *args->second);
            delete args;
            return 0;
        },
        new std::pair<UIState*, std::atomic<bool>*>(&state, &watcherRunning),
        0, nullptr);

    std::vector<uint8_t> dllInRam;

    auto onConnect = [&]() {
        state.AddLog("[INF] Connecting to " + std::string(state.serverHost)
                     + ":" + std::to_string(state.serverPort) + "...");

        struct ConnArgs {
            UIState*             st;
            uint8_t              hwid[32];
            uint8_t              loader[32];
            std::vector<uint8_t>* dll;
        };
        auto* args = new ConnArgs{};
        args->st  = &state;
        args->dll = &dllInRam;
        memcpy(args->hwid,   hwidHash,   32);
        memcpy(args->loader, loaderHash, 32);

        CreateThread(nullptr, 0,
            [](LPVOID p) -> DWORD {
                auto* a = static_cast<ConnArgs*>(p);
                *a->dll = ConnectAndFetchDll(
                    a->st->serverHost, a->st->serverPort,
                    a->hwid, a->loader, *a->st);
                if (!a->dll->empty())
                    a->st->AddLog("[INF] DLL ready in RAM ("
                                  + std::to_string(a->dll->size()) + " bytes)");
                delete a;
                return 0;
            }, args, 0, nullptr);
    };

    auto onInject = [&]() {
        if (!state.processFound)
        { state.AddLog("[ERR] Process not found"); return; }

        // Dev mode fallback: read DLL from disk if server hasn't delivered one
        if (dllInRam.empty() && strlen(state.dllPath) > 0)
        {
            HANDLE hF = CreateFileA(state.dllPath, GENERIC_READ, FILE_SHARE_READ,
                                     nullptr, OPEN_EXISTING, 0, nullptr);
            if (hF != INVALID_HANDLE_VALUE)
            {
                LARGE_INTEGER sz{};
                GetFileSizeEx(hF, &sz);
                dllInRam.resize((size_t)sz.QuadPart);
                DWORD r{};
                ReadFile(hF, dllInRam.data(), (DWORD)sz.QuadPart, &r, nullptr);
                CloseHandle(hF);
                state.AddLog("[WRN] Using local DLL (dev mode — no server encryption)");
            }
        }

        if (dllInRam.empty())
        { state.AddLog("[ERR] No DLL available"); return; }

        state.AddLog("[INF] Injecting into PID " + std::to_string(state.targetPid) + "...");

        struct InjectArgs { UIState* st; DWORD pid; std::vector<uint8_t> dll; };
        auto* args = new InjectArgs{ &state, state.targetPid, dllInRam };

        CreateThread(nullptr, 0,
            [](LPVOID p) -> DWORD {
                auto* a = static_cast<InjectArgs*>(p);
                HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, a->pid);
                if (!hProc)
                { a->st->AddLog("[ERR] OpenProcess failed"); delete a; return 1; }

                bool ok = ManualMapDll(hProc, a->dll);
                CloseHandle(hProc);

                a->st->injected = ok;
                a->st->AddLog(ok ? "[INF] Injection successful" : "[ERR] Injection failed");
                delete a;
                return 0;
            }, args, 0, nullptr);
    };

    RunUI(state, onConnect, onInject);

    watcherRunning = false;
    if (hWatcher) { WaitForSingleObject(hWatcher, 3000); CloseHandle(hWatcher); }

    Logger::Shutdown();
    return 0;
}
