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

// OpenSSL TLS — connects to SparkyServer with TLS when the server has a cert.
// If the server is running in plaintext mode, set useTls = false in UIState
// before connecting (or the SSL_connect will fail and connection will abort).
#include <openssl/ssl.h>
#include <openssl/err.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "psapi.lib")
// OpenSSL import libs linked via CMake (OpenSSL::SSL, OpenSSL::Crypto)

#include "Logger.h"
#include "ManualMap.h"
#include "UI.h"
#include "Protocol.h"
#include "TlsLayer.h"
#include "CertPin.h"

// ---------------------------------------------------------------------------
// Client-side SSL_CTX — one context shared across all connections.
// Initialized once in wWinMain; used (read-only) in ConnectAndFetchDll.
// ---------------------------------------------------------------------------
static SSL_CTX* g_loaderSslCtx = nullptr;

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
// Session state
// ---------------------------------------------------------------------------
struct ServerSession
{
    SOCKET   sock    = INVALID_SOCKET;
    SSL*     ssl     = nullptr;  // nullptr = plaintext dev mode
    uint64_t hdrKey  = 0;        // 0 until AuthOk is processed (AuthOk itself is plain)
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

    return NetSend(ss.sock, ss.ssl, &h, sizeof(h))
        && (buf.empty() || NetSend(ss.sock, ss.ssl, buf.data(), (int)buf.size()))
        && NetSend(ss.sock, ss.ssl, &crc, 4);
}

// RecvMsg: reads header + payload + CRC, verifies integrity, decrypts payload.
static bool RecvMsg(ServerSession& ss, MsgType& t,
                    std::vector<uint8_t>& pay, DWORD ms = 10000)
{
    MsgHeader h{};
    if (!NetRecv(ss.sock, ss.ssl, &h, sizeof(h), ms))              return false;
    if (h.Magic != PROTO_MAGIC || h.Version != PROTO_VERSION)      return false;

    pay.resize(h.Length);
    if (h.Length && !NetRecv(ss.sock, ss.ssl, pay.data(), h.Length, ms)) return false;

    uint32_t rc{};
    if (!NetRecv(ss.sock, ss.ssl, &rc, 4, ms)) return false;

    uint32_t lc = Crc32(reinterpret_cast<uint8_t*>(&h), sizeof(h));
    if (!pay.empty()) lc ^= Crc32(pay.data(), (uint32_t)pay.size());
    if (lc != rc) return false;

    if (ss.hdrKey && !pay.empty())
        XorStream(pay.data(), (uint32_t)pay.size(), ss.hdrKey);

    t = h.Type;
    return true;
}

// ---------------------------------------------------------------------------
// Post-delivery heartbeat thread — keeps the server session alive after the
// DLL has been received.  Takes ownership of the socket and SSL object.
// Sends a Heartbeat every 25 seconds; stops when the server closes.
// ---------------------------------------------------------------------------
struct HeartbeatArgs
{
    SOCKET   sock;
    SSL*     ssl;     // may be nullptr (plaintext mode)
    uint64_t hdrKey;
};

static DWORD WINAPI HeartbeatLoop(LPVOID p)
{
    auto* a = static_cast<HeartbeatArgs*>(p);
    ServerSession ss{};
    ss.sock   = a->sock;
    ss.ssl    = a->ssl;
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

    if (ss.ssl) { SSL_shutdown(ss.ssl); SSL_free(ss.ssl); }
    closesocket(ss.sock);
    WSACleanup();
    return 0;
}

// ---------------------------------------------------------------------------
// ConnectAndFetchDll
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

    // Cleanup helper used at every error path below.
    // On success the socket/ssl ownership is transferred to HeartbeatLoop.
    auto cleanup = [&]() {
        if (ss.ssl) { SSL_shutdown(ss.ssl); SSL_free(ss.ssl); ss.ssl = nullptr; }
        closesocket(ss.sock); ss.sock = INVALID_SOCKET;
        WSACleanup();
    };

    // ------------------------------------------------------------------
    // TLS handshake (if g_loaderSslCtx was initialised)
    // ------------------------------------------------------------------
    if (g_loaderSslCtx)
    {
        ss.ssl = SSL_new(g_loaderSslCtx);
        if (!ss.ssl)
        {
            state.AddLog("[ERR] SSL_new failed");
            cleanup(); return {};
        }
        SSL_set_fd(ss.ssl, (int)ss.sock);
        SSL_set_tlsext_host_name(ss.ssl, host); // SNI hint

        if (SSL_connect(ss.ssl) <= 0)
        {
            state.AddLog("[ERR] TLS handshake failed: " + TlsLastError());
            cleanup(); return {};
        }

        // ------------------------------------------------------------------
        // Certificate pinning — verify the server cert fingerprint matches
        // the value compiled into SPARKY_CERT_PIN (CertPin.h).
        //
        // Prevents MITM attacks via Fiddler / Charles / mitmproxy / rogue CA:
        // even if the attacker installs a trusted root on the victim machine,
        // they cannot forge a certificate that matches our specific pin.
        // ------------------------------------------------------------------
        if (SPARKY_CERT_PIN && *SPARKY_CERT_PIN)
        {
            X509* cert = SSL_get_peer_certificate(ss.ssl);
            if (!cert)
            {
                state.AddLog("[ERR] Server presented no certificate");
                cleanup(); return {};
            }

            // SHA-256 of the DER-encoded certificate (same as
            // `openssl x509 -fingerprint -sha256`)
            uint8_t digest[32]{};
            unsigned int digestLen = 32;
            const int digestOk =
                X509_digest(cert, EVP_sha256(), digest, &digestLen);
            X509_free(cert);

            if (!digestOk)
            {
                state.AddLog("[ERR] Certificate digest computation failed");
                cleanup(); return {};
            }

            // Convert digest to lowercase hex for string comparison
            static const char h[] = "0123456789abcdef";
            char fingerprint[65]{};
            for (int i = 0; i < 32; ++i)
            {
                fingerprint[i*2]   = h[digest[i] >> 4];
                fingerprint[i*2+1] = h[digest[i] & 0xF];
            }

            if (strncmp(fingerprint, SPARKY_CERT_PIN, 64) != 0)
            {
                state.AddLog("[ERR] Certificate pin mismatch — possible MITM!");
                state.AddLog(std::string("[ERR] Expected: ") + SPARKY_CERT_PIN);
                state.AddLog(std::string("[ERR] Got:      ") + fingerprint);
                cleanup(); return {};
            }
            state.AddLog("[INF] Certificate pin OK");
        }
        else
        {
            // No pin configured — TLS encrypts traffic but does not
            // authenticate the server. Update CertPin.h before shipping.
            state.AddLog("[WRN] No certificate pin set — MITM not prevented");
        }

        state.AddLog("[INF] TLS established");
    }
    else
    {
        state.AddLog("[WRN] TLS unavailable — connecting in plaintext");
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
        cleanup(); return {};
    }

    // ------------------------------------------------------------------
    // Step 3: Receive AuthOk or AuthFail  (PLAIN — hdrKey still 0)
    // ------------------------------------------------------------------
    {
        MsgType t{};
        std::vector<uint8_t> pay;
        if (!RecvMsg(ss, t, pay))
        {
            state.AddLog("[ERR] No response from server");
            cleanup(); return {};
        }

        if (t == MsgType::AuthFail)
        {
            state.AddLog("[ERR] Auth rejected by server (check license / HWID)");
            cleanup(); return {};
        }

        if (t != MsgType::AuthOk || pay.size() < sizeof(AuthOkPayload))
        {
            state.AddLog("[ERR] Unexpected message during auth");
            cleanup(); return {};
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
    // ------------------------------------------------------------------
    BinaryReadyPayload br{};
    {
        MsgType t{};
        std::vector<uint8_t> pay;
        if (!RecvMsg(ss, t, pay))
        {
            state.AddLog("[ERR] No post-auth message received");
            cleanup(); return {};
        }

        if (t == MsgType::Config)
        {
            state.AddLog("[INF] Config received (" + std::to_string(pay.size()) + " bytes)");

            if (!RecvMsg(ss, t, pay))
            {
                state.AddLog("[ERR] No BinaryReady after Config");
                cleanup(); return {};
            }
        }

        if (t != MsgType::BinaryReady || pay.size() < sizeof(BinaryReadyPayload))
        {
            state.AddLog("[ERR] Expected BinaryReady");
            cleanup(); return {};
        }

        br = *reinterpret_cast<const BinaryReadyPayload*>(pay.data());
        state.AddLog("[INF] DLL incoming: " + std::to_string(br.TotalBytes)
                     + " bytes, " + std::to_string(br.NumChunks)
                     + " chunks, HB every " + std::to_string(br.ChunksPerHeartbeat));
    }

    if (br.TotalBytes == 0 || br.NumChunks == 0 || br.ChunksPerHeartbeat == 0)
    {
        state.AddLog("[ERR] Invalid BinaryReady parameters");
        cleanup(); return {};
    }

    // ------------------------------------------------------------------
    // Step 6: Receive chunks with rolling-key decryption + heartbeat sync
    // ------------------------------------------------------------------
    std::vector<uint8_t> dllBuf;
    dllBuf.reserve(br.TotalBytes);

    uint64_t rollingKey = ss.dllKey;

    for (uint32_t c = 0; c < br.NumChunks; ++c)
    {
        MsgType ct{};
        std::vector<uint8_t> cp;
        if (!RecvMsg(ss, ct, cp, 20000) || ct != MsgType::BinaryChunk)
        {
            state.AddLog("[ERR] Expected BinaryChunk at " + std::to_string(c));
            cleanup(); return {};
        }

        XorStream(cp.data(), (uint32_t)cp.size(), rollingKey);
        dllBuf.insert(dllBuf.end(), cp.begin(), cp.end());

        if ((c + 1) % br.ChunksPerHeartbeat == 0 || c + 1 == br.NumChunks)
        {
            HeartbeatPayload hb{};
            if (!CryptRandBytes(hb.Nonce, 16))
            {
                state.AddLog("[ERR] CryptRandBytes failed — cannot generate heartbeat nonce");
                cleanup(); return {};
            }

            if (!SendMsg(ss, MsgType::Heartbeat, &hb, sizeof(hb)))
            {
                state.AddLog("[ERR] Heartbeat send failed at chunk " + std::to_string(c));
                cleanup(); return {};
            }

            MsgType at{}; std::vector<uint8_t> ap;
            if (!RecvMsg(ss, at, ap, HEARTBEAT_DEADLINE_MS) || at != MsgType::Ack)
            {
                state.AddLog("[ERR] No Ack after heartbeat at chunk " + std::to_string(c));
                cleanup(); return {};
            }

            rollingKey = RollKey(rollingKey, hb.Nonce);
        }
    }

    // ------------------------------------------------------------------
    // Step 7: Receive BinaryEnd
    // ------------------------------------------------------------------
    {
        MsgType et{}; std::vector<uint8_t> ep;
        if (!RecvMsg(ss, et, ep, 10000) || et != MsgType::BinaryEnd)
            state.AddLog("[WRN] Missing BinaryEnd — continuing anyway");
        else
            state.AddLog("[INF] Transfer complete");
    }

    if (dllBuf.size() != br.TotalBytes)
    {
        state.AddLog("[ERR] DLL size mismatch: got " + std::to_string(dllBuf.size())
                     + " expected " + std::to_string(br.TotalBytes));
        cleanup(); return {};
    }

    state.AddLog("[INF] DLL decrypted in RAM (" + std::to_string(dllBuf.size()) + " bytes)");

    // ------------------------------------------------------------------
    // Step 8: Hand socket + SSL to background heartbeat thread.
    // Thread owns both and will SSL_shutdown + SSL_free + closesocket.
    // ------------------------------------------------------------------
    auto* hbArgs = new HeartbeatArgs{ ss.sock, ss.ssl, ss.hdrKey };
    ss.sock = INVALID_SOCKET; // thread now owns
    ss.ssl  = nullptr;        // thread now owns
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

    // Initialise OpenSSL and create a client TLS context.
    // SSL_VERIFY_NONE is used for research/dev — in production you would load
    // a CA cert and use SSL_VERIFY_PEER to authenticate the server certificate.
    OPENSSL_init_ssl(0, nullptr);
    g_loaderSslCtx = SSL_CTX_new(TLS_client_method());
    if (g_loaderSslCtx)
        SSL_CTX_set_verify(g_loaderSslCtx, SSL_VERIFY_NONE, nullptr);

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

    if (g_loaderSslCtx) { SSL_CTX_free(g_loaderSslCtx); g_loaderSslCtx = nullptr; }

    Logger::Shutdown();
    return 0;
}
