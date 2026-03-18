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
#include <thread>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "psapi.lib")

#include "Logger.h"
#include "ManualMap.h"
#include "UI.h"
#include "Protocol.h"

// ---------------------------------------------------------------------------
// HWID: SHA-256 of machine GUID via CryptAPI (no LoadLibrary call — crypt32
// is linked at compile time via the pragma above).
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
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr,
                               PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return false;

    HCRYPTHASH hHash{};
    ok = false;
    if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
    {
        if (CryptHashData(hHash, (BYTE*)guid, cb - sizeof(wchar_t), 0))
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
// Find process by name — uses CreateToolhelp32Snapshot, no LoadLibrary.
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
// Minimal raw socket helpers — avoids C++ stream wrappers that may call
// LoadLibraryExW when initialising locale on some MSVC runtimes.
// ---------------------------------------------------------------------------
static bool RawSend(SOCKET s, const void* d, int n)
{
    const char* p = (const char*)d; int sent = 0;
    while (sent < n) { int r = send(s, p+sent, n-sent, 0); if (r<=0) return false; sent+=r; }
    return true;
}
static bool RawRecv(SOCKET s, void* d, int n, DWORD ms = 10000)
{
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&ms, sizeof(ms));
    char* p = (char*)d; int got = 0;
    while (got < n) { int r = recv(s, p+got, n-got, 0); if (r<=0) return false; got+=r; }
    return true;
}

// ---------------------------------------------------------------------------
// Server session
// ---------------------------------------------------------------------------
struct ServerSession
{
    SOCKET   sock = INVALID_SOCKET;
    uint64_t hdrKey  = 0;  // key for header XOR (derived from token, salt=0)
    uint64_t dllKey  = 0;  // key for DLL stream (derived from token, salt=1)
    uint8_t  token[16]{};
};

static bool SendMsg(ServerSession& ss, MsgType t,
                     const void* pay=nullptr, uint16_t len=0)
{
    MsgHeader h{}; h.Magic=PROTO_MAGIC; h.Version=PROTO_VERSION; h.Type=t; h.Length=len;
    std::vector<uint8_t> buf(len);
    if (pay && len) memcpy(buf.data(), pay, len);
    if (ss.hdrKey && !buf.empty()) XorStream(buf.data(), buf.size(), ss.hdrKey);
    uint32_t crc = Crc32((uint8_t*)&h, sizeof(h));
    if (!buf.empty()) crc ^= Crc32(buf.data(), buf.size());
    return RawSend(ss.sock,&h,sizeof(h))
        && (buf.empty()||RawSend(ss.sock,buf.data(),buf.size()))
        && RawSend(ss.sock,&crc,4);
}
static bool RecvMsg(ServerSession& ss, MsgType& t,
                     std::vector<uint8_t>& pay, DWORD ms=10000)
{
    MsgHeader h{};
    if (!RawRecv(ss.sock,&h,sizeof(h),ms)) return false;
    if (h.Magic!=PROTO_MAGIC||h.Version!=PROTO_VERSION) return false;
    pay.resize(h.Length);
    if (h.Length&&!RawRecv(ss.sock,pay.data(),h.Length,ms)) return false;
    uint32_t rc{}; if (!RawRecv(ss.sock,&rc,4,ms)) return false;
    uint32_t lc=Crc32((uint8_t*)&h,sizeof(h));
    if (!pay.empty()) lc^=Crc32(pay.data(),pay.size());
    if (lc!=rc) return false;
    if (ss.hdrKey&&!pay.empty()) XorStream(pay.data(),pay.size(),ss.hdrKey);
    t = h.Type; return true;
}

// ---------------------------------------------------------------------------
// ConnectAndFetchDll:
//   1. Authenticate with server
//   2. Receive encrypted DLL chunks in RAM
//   3. Decrypt with session-derived key
//   Returns decrypted DLL bytes (empty on failure).
// ---------------------------------------------------------------------------
static std::vector<uint8_t> ConnectAndFetchDll(
    const char* host, int port,
    const uint8_t hwidHash[32],
    UIState& state)
{
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2,2), &wsa);

    ServerSession ss{};
    ss.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ss.sock == INVALID_SOCKET) return {};

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    DWORD connTo = 5000;
    setsockopt(ss.sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&connTo, sizeof(connTo));

    if (connect(ss.sock, (sockaddr*)&addr, sizeof(addr)) != 0)
    {
        state.AddLog("[ERR] Cannot connect to server");
        closesocket(ss.sock);
        WSACleanup();
        return {};
    }

    // --- Send Hello ---
    MsgHeader hdr{}; hdr.Magic=PROTO_MAGIC; hdr.Version=PROTO_VERSION;
    hdr.Type=MsgType::Hello; hdr.Length=sizeof(HelloPayload);
    HelloPayload hello{}; memcpy(hello.HwidHash,hwidHash,32); hello.BuildId=0x0001'0000;
    uint32_t crc = Crc32((uint8_t*)&hdr,sizeof(hdr))^Crc32((uint8_t*)&hello,sizeof(hello));
    RawSend(ss.sock,&hdr,sizeof(hdr));
    RawSend(ss.sock,&hello,sizeof(hello));
    RawSend(ss.sock,&crc,4);

    // --- Recv AuthOk ---
    MsgHeader rh{};
    if (!RawRecv(ss.sock,&rh,sizeof(rh)) || rh.Magic!=PROTO_MAGIC || rh.Type!=MsgType::AuthOk)
    {
        state.AddLog("[ERR] Auth failed");
        closesocket(ss.sock); WSACleanup(); return {};
    }
    std::vector<uint8_t> authPay(rh.Length);
    if (rh.Length) RawRecv(ss.sock,authPay.data(),rh.Length);
    uint32_t dummy{}; RawRecv(ss.sock,&dummy,4);

    if (authPay.size() >= sizeof(AuthOkPayload))
    {
        auto& aok = *(AuthOkPayload*)authPay.data();
        memcpy(ss.token, aok.SessionToken, 16);
        ss.hdrKey = DeriveKey(ss.token, 0);
        ss.dllKey = DeriveKey(ss.token, 1);
        state.serverConnected = true;
        state.AddLog("[INF] Authenticated");
    }

    // --- Skip optional Config ---
    {
        MsgType t{}; std::vector<uint8_t> p;
        if (RecvMsg(ss, t, p) && t == MsgType::Config)
            state.AddLog("[INF] Config received");
        else
        {
            // Not a Config — might be BinaryReady; fall through
            if (t == MsgType::BinaryReady) goto handle_binary;
            goto done;
        }
    }

    // --- Receive BinaryReady + chunks ---
    {
        MsgType t{}; std::vector<uint8_t> p;
        if (!RecvMsg(ss, t, p) || t != MsgType::BinaryReady
            || p.size() < sizeof(BinaryReadyPayload))
            goto done;

        handle_binary:;
        auto& br = *(BinaryReadyPayload*)p.data();
        state.AddLog("[INF] Receiving DLL (" + std::to_string(br.TotalBytes) + " bytes)...");

        std::vector<uint8_t> encDll;
        encDll.reserve(br.TotalBytes);

        for (uint32_t c = 0; c < br.NumChunks; ++c)
        {
            MsgType ct{}; std::vector<uint8_t> cp;
            if (!RecvMsg(ss, ct, cp) || ct != MsgType::BinaryChunk) goto done;
            encDll.insert(encDll.end(), cp.begin(), cp.end());
        }

        // Expect BinaryEnd
        { MsgType et{}; std::vector<uint8_t> ep; RecvMsg(ss,et,ep); }

        // Decrypt DLL in RAM using session-derived key (unique per auth)
        XorStream(encDll.data(), (uint32_t)encDll.size(), ss.dllKey);
        state.AddLog("[INF] DLL decrypted in RAM");

        closesocket(ss.sock);
        WSACleanup();
        return encDll;
    }

    done:
    closesocket(ss.sock);
    WSACleanup();
    return {};
}

// ---------------------------------------------------------------------------
// Process watcher — runs on a background thread, no heap/CRT allocs that
// could trigger implicit LoadLibrary calls.
// ---------------------------------------------------------------------------
static void ProcessWatcher(UIState& state, std::atomic<bool>& running)
{
    while (running.load())
    {
        DWORD pid = FindProcessByName(state.processName);
        state.processFound = pid != 0;
        state.targetPid    = pid;
        Sleep(1000);
    }
}

// ---------------------------------------------------------------------------
// Entry point — wWinMain to avoid console window (WIN32 subsystem).
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    Logger::Init("SparkyLoader.log");

    UIState state{};
    state.AddLog("[INF] Sparky ready");

    uint8_t hwidHash[32]{};
    if (!GetHwidHash(hwidHash))
        state.AddLog("[WRN] HWID hash failed");

    std::atomic<bool> watcherRunning = true;
    // Use CreateThread directly — avoids any potential std::thread runtime init
    // that might call LoadLibraryExW on some MSVC builds.
    HANDLE hWatcher = CreateThread(nullptr, 0,
        [](LPVOID p) -> DWORD {
            auto* args = (std::pair<UIState*, std::atomic<bool>*>*)p;
            ProcessWatcher(*args->first, *args->second);
            delete args;
            return 0;
        },
        new std::pair<UIState*, std::atomic<bool>*>(&state, &watcherRunning),
        0, nullptr);

    // Holds decrypted DLL bytes received from server (in RAM only, never disk)
    std::vector<uint8_t> dllInRam;

    auto onConnect = [&]() {
        state.AddLog("[INF] Connecting to " + std::string(state.serverHost)
                     + ":" + std::to_string(state.serverPort) + "...");

        CreateThread(nullptr, 0,
            [](LPVOID p) -> DWORD {
                auto* args = (std::tuple<UIState*, uint8_t*, std::vector<uint8_t>*>*)p;
                auto& [st, hwid, dll] = *args;
                *dll = ConnectAndFetchDll(st->serverHost, st->serverPort, hwid, *st);
                if (!dll->empty())
                    st->AddLog("[INF] DLL ready in RAM (" + std::to_string(dll->size()) + " bytes)");
                delete args;
                return 0;
            },
            new std::tuple<UIState*, uint8_t*, std::vector<uint8_t>*>(
                &state, hwidHash, &dllInRam),
            0, nullptr);
    };

    auto onInject = [&]() {
        if (!state.processFound)
        { state.AddLog("[ERR] Process not found"); return; }

        // If no DLL from server, fall back to file on disk (dev mode)
        if (dllInRam.empty() && strlen(state.dllPath) > 0)
        {
            HANDLE hF = CreateFileA(state.dllPath, GENERIC_READ, FILE_SHARE_READ,
                                     nullptr, OPEN_EXISTING, 0, nullptr);
            if (hF != INVALID_HANDLE_VALUE)
            {
                LARGE_INTEGER sz{}; GetFileSizeEx(hF, &sz);
                dllInRam.resize((size_t)sz.QuadPart);
                DWORD r{}; ReadFile(hF, dllInRam.data(), (DWORD)sz.QuadPart, &r, nullptr);
                CloseHandle(hF);
                state.AddLog("[WRN] Using local DLL (dev mode — not encrypted)");
            }
        }

        if (dllInRam.empty())
        { state.AddLog("[ERR] No DLL available"); return; }

        state.AddLog("[INF] Injecting into PID " + std::to_string(state.targetPid) + "...");

        struct InjectArgs { UIState* st; DWORD pid; std::vector<uint8_t> dll; };
        auto* args = new InjectArgs{ &state, state.targetPid, dllInRam };

        CreateThread(nullptr, 0,
            [](LPVOID p) -> DWORD {
                auto* a = (InjectArgs*)p;
                HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, a->pid);
                if (!hProc) { a->st->AddLog("[ERR] OpenProcess failed"); delete a; return 1; }

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
