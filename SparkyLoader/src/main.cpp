// main.cpp — Sparky Loader
#include <Windows.h>
#include <TlHelp32.h>
#include <wincrypt.h>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <filesystem>

#include "Logger.h"
#include "ManualMap.h"
#include "UI.h"
#include "Protocol.h"

// Communication inline (no separate .cpp needed — small enough)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "psapi.lib")

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// HWID: SHA-256 of machine GUID
// ---------------------------------------------------------------------------
static bool GetHwidHash(uint8_t out[32])
{
    HKEY hKey{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    wchar_t guid[64]{};
    DWORD cb   = sizeof(guid);
    DWORD type = REG_SZ;
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
        if (CryptHashData(hHash, reinterpret_cast<BYTE*>(guid), cb - sizeof(wchar_t), 0))
        {
            DWORD hLen = 32;
            ok = CryptGetHashParam(hHash, HP_HASHVAL, out, &hLen, 0) == TRUE;
        }
        CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);
    return ok;
}

// ---------------------------------------------------------------------------
// Find a process by name
// ---------------------------------------------------------------------------
static DWORD FindProcess(const char* name)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32A pe{ sizeof(pe) };
    DWORD pid = 0;
    if (Process32FirstA(hSnap, &pe))
    {
        do {
            if (_stricmp(pe.szExeFile, name) == 0)
            { pid = pe.th32ProcessID; break; }
        } while (Process32NextA(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return pid;
}

// ---------------------------------------------------------------------------
// Simple blocking server authenticate + config fetch
// ---------------------------------------------------------------------------
static bool ServerConnect(const char* host, int port,
                           uint8_t hwidHash[32], UIState& state)
{
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;

    DWORD to = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0)
    {
        closesocket(sock);
        return false;
    }

    // Send Hello
    MsgHeader hdr{};
    hdr.Magic   = PROTO_MAGIC;
    hdr.Version = PROTO_VERSION;
    hdr.Type    = MsgType::Hello;
    hdr.Length  = sizeof(HelloPayload);

    HelloPayload hello{};
    memcpy(hello.HwidHash, hwidHash, 32);
    hello.BuildId = 0x0001'0000;

    uint32_t crc = Crc32((uint8_t*)&hdr, sizeof(hdr)) ^ Crc32((uint8_t*)&hello, sizeof(hello));
    send(sock, (char*)&hdr,   sizeof(hdr),   0);
    send(sock, (char*)&hello, sizeof(hello), 0);
    send(sock, (char*)&crc,   sizeof(crc),   0);

    // Recv AuthOk
    MsgHeader rHdr{};
    int got = recv(sock, (char*)&rHdr, sizeof(rHdr), MSG_WAITALL);
    if (got != sizeof(rHdr) || rHdr.Magic != PROTO_MAGIC || rHdr.Type != MsgType::AuthOk)
    {
        closesocket(sock);
        return false;
    }

    state.AddLog("[INF] Server: authenticated");

    // Optionally recv config
    if (rHdr.Length >= sizeof(AuthOkPayload))
    {
        std::vector<uint8_t> pay(rHdr.Length);
        recv(sock, (char*)pay.data(), rHdr.Length, MSG_WAITALL);
        uint32_t dummy{};
        recv(sock, (char*)&dummy, 4, MSG_WAITALL);
    }

    closesocket(sock);
    WSACleanup();
    return true;
}

// ---------------------------------------------------------------------------
// Background process watcher
// ---------------------------------------------------------------------------
static void ProcessWatcher(UIState& state, std::atomic<bool>& running)
{
    while (running.load())
    {
        DWORD pid = FindProcess(state.processName);
        state.processFound = pid != 0;
        state.targetPid    = pid;
        Sleep(1000);
    }
}

// ---------------------------------------------------------------------------
// wmain
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    Logger::Init("SparkyLoader.log");

    UIState state{};
    state.AddLog("[INF] Sparky started");

    // Check if module file exists
    state.dllReady = fs::exists(state.dllPath);
    if (!state.dllReady)
        state.AddLog(std::format("[WRN] Module not found: {}", state.dllPath));

    uint8_t hwidHash[32]{};
    GetHwidHash(hwidHash);

    // Background process watcher
    std::atomic<bool> watcherRunning = true;
    std::thread watcher(ProcessWatcher, std::ref(state), std::ref(watcherRunning));

    // Callbacks
    auto onConnect = [&]() {
        state.AddLog(std::format("[INF] Connecting to {}:{}...",
                                  state.serverHost, state.serverPort));
        std::thread([&]() {
            bool ok = ServerConnect(state.serverHost, state.serverPort,
                                    hwidHash, state);
            state.serverConnected = ok;
            state.AddLog(ok ? "[INF] Connected" : "[ERR] Connection failed");
        }).detach();
    };

    auto onInject = [&]() {
        if (!state.processFound)
        { state.AddLog("[ERR] Process not found"); return; }

        // Re-check file
        state.dllReady = fs::exists(state.dllPath);
        if (!state.dllReady)
        { state.AddLog(std::format("[ERR] Module not found: {}", state.dllPath)); return; }

        state.AddLog(std::format("[INF] Injecting into PID {}...", state.targetPid));

        std::thread([&]() {
            HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, state.targetPid);
            if (!hProc)
            {
                state.AddLog("[ERR] OpenProcess failed");
                return;
            }

            std::wstring wPath(state.dllPath, state.dllPath + strlen(state.dllPath));
            bool ok = ManualMapDllFile(hProc, wPath);
            CloseHandle(hProc);

            state.injected = ok;
            state.AddLog(ok ? "[INF] Injection successful" : "[ERR] Injection failed");
        }).detach();
    };

    RunUI(state, onConnect, onInject);

    watcherRunning = false;
    if (watcher.joinable()) watcher.join();

    Logger::Shutdown();
    return 0;
}
