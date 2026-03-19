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
#include <ctime>

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
#include "WebSocket.h"
#include "CertPin.h"
#include "StringCrypt.h"

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
// HashBytes — SHA-256 of arbitrary data via CryptAPI.
// Used to hash the user's password before sending to the server.
// ---------------------------------------------------------------------------
static bool HashBytes(const void* data, size_t len, uint8_t out[32])
{
    HCRYPTPROV hProv{};
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return false;
    HCRYPTHASH hHash{};
    bool ok = false;
    if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
    {
        if (CryptHashData(hHash, static_cast<const BYTE*>(data), static_cast<DWORD>(len), 0))
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
    PROCESSENTRY32W pe{ sizeof(pe) };
    wchar_t wname[MAX_PATH]{};
    MultiByteToWideChar(CP_ACP, 0, name, -1, wname, MAX_PATH);
    DWORD pid = 0;
    if (Process32FirstW(hSn, &pe))
        do { if (_wcsicmp(pe.szExeFile, wname) == 0) { pid = pe.th32ProcessID; break; } }
        while (Process32NextW(hSn, &pe));
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
    bool     wsMode  = false;    // true after WebSocket upgrade handshake
};

// SendMsg: encrypts payload with hdrKey if non-zero, computes CRC-32.
// In WebSocket mode the entire binary message is wrapped in a masked WS frame.
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

    if (ss.wsMode) {
        // Pack into one buffer and send as a single masked WS binary frame.
        std::vector<uint8_t> msg(sizeof(h) + buf.size() + 4);
        memcpy(msg.data(), &h, sizeof(h));
        if (!buf.empty()) memcpy(msg.data() + sizeof(h), buf.data(), buf.size());
        memcpy(msg.data() + sizeof(h) + buf.size(), &crc, 4);
        return WsSendFrameMasked(ss.sock, ss.ssl, msg.data(), msg.size());
    }

    return NetSend(ss.sock, ss.ssl, &h, sizeof(h))
        && (buf.empty() || NetSend(ss.sock, ss.ssl, buf.data(), (int)buf.size()))
        && NetSend(ss.sock, ss.ssl, &crc, 4);
}

// RecvMsg: reads header + payload + CRC, verifies integrity, decrypts payload.
// In WebSocket mode it first receives a WS frame and then parses the binary message from it.
static bool RecvMsg(ServerSession& ss, MsgType& t,
                    std::vector<uint8_t>& pay, DWORD ms = 10000)
{
    if (ss.wsMode) {
        std::vector<uint8_t> frame;
        if (!WsRecvFrame(ss.sock, ss.ssl, frame, ms)) return false;
        if (frame.size() < sizeof(MsgHeader) + 4) return false;
        MsgHeader h{};
        memcpy(&h, frame.data(), sizeof(h));
        if (h.Magic != PROTO_MAGIC || h.Version != PROTO_VERSION) return false;
        if (sizeof(MsgHeader) + h.Length + 4 > frame.size()) return false;
        pay.resize(h.Length);
        if (h.Length) memcpy(pay.data(), frame.data() + sizeof(h), h.Length);
        uint32_t rc{};
        memcpy(&rc, frame.data() + sizeof(h) + h.Length, 4);
        uint32_t lc = Crc32(reinterpret_cast<uint8_t*>(&h), sizeof(h));
        if (!pay.empty()) lc ^= Crc32(pay.data(), (uint32_t)pay.size());
        if (lc != rc) return false;
        if (ss.hdrKey && !pay.empty())
            XorStream(pay.data(), (uint32_t)pay.size(), ss.hdrKey);
        t = h.Type;
        return true;
    }

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
// ReceiveDll — called by SessionThread after sending RequestDll.
// Reads optional Config, then BinaryReady + chunks + BinaryEnd.
// Returns true on protocol success; dllBuf is empty if server has no DLL.
// ---------------------------------------------------------------------------
static bool ReceiveDll(ServerSession& ss, std::vector<uint8_t>& dllBuf, UIState& state)
{
    MsgType t{}; std::vector<uint8_t> p;
    if (!RecvMsg(ss, t, p, 30000))
    {
        state.AddLog("[ERR] No response from server after RequestDll");
        return false;
    }

    if (t == MsgType::Config)
    {
        state.AddLog("[INF] Config received (" + std::to_string(p.size()) + " bytes)");
        if (!RecvMsg(ss, t, p, 30000))
        {
            state.AddLog("[ERR] No BinaryReady after Config");
            return false;
        }
    }

    // Server sends BinaryEnd (TotalBytes=0) when it has no DLL loaded.
    if (t == MsgType::BinaryEnd)
    {
        state.AddLog("[WRN] Server has no DLL loaded yet");
        return true; // dllBuf stays empty; session remains alive
    }

    if (t != MsgType::BinaryReady || p.size() < sizeof(BinaryReadyPayload))
    {
        state.AddLog("[ERR] Expected BinaryReady — got unexpected message");
        return false;
    }

    BinaryReadyPayload br = *reinterpret_cast<const BinaryReadyPayload*>(p.data());
    if (br.TotalBytes == 0 || br.NumChunks == 0)
    {
        state.AddLog("[WRN] Server reported empty DLL (TotalBytes=0)");
        return true; // dllBuf empty; session alive
    }

    state.AddLog("[INF] DLL incoming: " + std::to_string(br.TotalBytes)
                 + " bytes, " + std::to_string(br.NumChunks) + " chunks");
    dllBuf.reserve(br.TotalBytes);

    uint64_t rollingKey = ss.dllKey;
    for (uint32_t c = 0; c < br.NumChunks; ++c)
    {
        MsgType ct{}; std::vector<uint8_t> cp;
        if (!RecvMsg(ss, ct, cp, 20000) || ct != MsgType::BinaryChunk)
        {
            state.AddLog("[ERR] Expected BinaryChunk at chunk " + std::to_string(c));
            return false;
        }
        XorStream(cp.data(), (uint32_t)cp.size(), rollingKey);
        dllBuf.insert(dllBuf.end(), cp.begin(), cp.end());
        state.downloadProgress = static_cast<float>(c + 1) / static_cast<float>(br.NumChunks);

        if ((c + 1) % br.ChunksPerHeartbeat == 0 || c + 1 == br.NumChunks)
        {
            HeartbeatPayload hb{};
            RAND_bytes(hb.Nonce, 16);
            if (!SendMsg(ss, MsgType::Heartbeat, &hb, sizeof(hb)))
            {
                state.AddLog("[ERR] Heartbeat send failed during DLL transfer");
                return false;
            }
            MsgType at{}; std::vector<uint8_t> ap;
            if (!RecvMsg(ss, at, ap, HEARTBEAT_DEADLINE_MS) || at != MsgType::Ack)
            {
                state.AddLog("[ERR] No Ack after heartbeat");
                return false;
            }
            rollingKey = RollKey(rollingKey, hb.Nonce);
        }
    }

    MsgType et{}; std::vector<uint8_t> ep;
    if (!RecvMsg(ss, et, ep, 10000) || et != MsgType::BinaryEnd)
        state.AddLog("[WRN] Missing BinaryEnd — continuing anyway");
    else
        state.AddLog("[INF] DLL transfer complete");

    if (dllBuf.size() != br.TotalBytes)
    {
        state.AddLog("[ERR] DLL size mismatch: got " + std::to_string(dllBuf.size())
                     + " expected " + std::to_string(br.TotalBytes));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SessionThread — long-running background thread that owns the server connection.
//
// Phase 1: Connect → TLS → WebSocket → Authenticate.
//          Sets state.loggedIn = true and state.connecting = false on success.
//
// Phase 2: Session loop.
//          Sends a Heartbeat every 25 s to keep the session alive.
//          When state.dllRequested is set (user pressed Inject), sends a
//          RequestDll message to the server and receives the DLL stream.
//          After DLL delivery, sets state.dllReady = true and writes the
//          bytes into *dll.
//
// Phase 3: Cleanup on disconnect.
//          Resets loggedIn / serverConnected so the UI shows a fresh login.
// ---------------------------------------------------------------------------
struct SessionArgs
{
    UIState*              state;
    uint8_t               hwid[32];
    uint8_t               loader[32];
    uint8_t               pwHash[32];
    std::vector<uint8_t>* dll;
};

static DWORD WINAPI SessionThread(LPVOID p)
{
    auto* a     = static_cast<SessionArgs*>(p);
    UIState& state = *a->state;

    // Shared cleanup — called on every error path before returning.
    ServerSession ss{};
    auto netCleanup = [&]() {
        if (ss.ssl) { SSL_shutdown(ss.ssl); SSL_free(ss.ssl); ss.ssl = nullptr; }
        if (ss.sock != INVALID_SOCKET) { closesocket(ss.sock); ss.sock = INVALID_SOCKET; }
        WSACleanup();
    };
    auto done = [&]() {
        netCleanup();
        // serverConnected is NOT cleared here — PingerThread manages it
        // independently so the dot stays green as long as the server is reachable.
        state.loggedIn   = false;
        state.connecting = false;
        state.loggingIn  = false;
        delete a;
    };

    // ── Phase 1: Connect ──────────────────────────────────────────────────
    state.serverConnected = false;

    WSADATA wsa{};
    WSAStartup(MAKEWORD(2,2), &wsa);

    const char* host = state.serverHost;
    const int   port = state.serverPort;

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%d", port);
    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portStr, &hints, &res) != 0 || !res)
    {
        state.AddLog("[ERR] DNS resolution failed for " + std::string(host));
        done(); return 1;
    }

    ss.sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (ss.sock == INVALID_SOCKET) { freeaddrinfo(res); done(); return 1; }

    DWORD connTo = 5000;
    setsockopt(ss.sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&connTo, sizeof(connTo));

    if (connect(ss.sock, res->ai_addr, (int)res->ai_addrlen) != 0)
    {
        state.AddLog("[ERR] Cannot connect to " + std::string(host));
        freeaddrinfo(res); done(); return 1;
    }
    freeaddrinfo(res);

    // ── Phase 1b: TLS handshake ───────────────────────────────────────────
    if (g_loaderSslCtx)
    {
        ss.ssl = SSL_new(g_loaderSslCtx);
        if (!ss.ssl) { state.AddLog("[ERR] SSL_new failed"); done(); return 1; }
        SSL_set_fd(ss.ssl, (int)ss.sock);
        SSL_set_tlsext_host_name(ss.ssl, host);

        if (SSL_connect(ss.ssl) <= 0)
        {
            state.AddLog("[ERR] TLS handshake failed: " + TlsLastError());
            done(); return 1;
        }

        if (SPARKY_CERT_PIN && *SPARKY_CERT_PIN)
        {
            X509* cert = SSL_get_peer_certificate(ss.ssl);
            if (!cert) { state.AddLog("[ERR] Server presented no certificate"); done(); return 1; }
            uint8_t digest[32]{}; unsigned int digestLen = 32;
            const int digestOk = X509_digest(cert, EVP_sha256(), digest, &digestLen);
            X509_free(cert);
            if (!digestOk) { state.AddLog("[ERR] Certificate digest failed"); done(); return 1; }
            static const char h[] = "0123456789abcdef";
            char fp[65]{};
            for (int i = 0; i < 32; ++i) { fp[i*2] = h[digest[i]>>4]; fp[i*2+1] = h[digest[i]&0xF]; }
            if (strncmp(fp, SPARKY_CERT_PIN, 64) != 0)
            {
                state.AddLog("[ERR] Certificate pin mismatch — possible MITM!");
                done(); return 1;
            }
            state.AddLog("[INF] Certificate pin OK");
        }
        else
        {
            state.AddLog("[WRN] No certificate pin set — MITM not prevented");
        }
    }

    // ── Phase 1c: WebSocket upgrade + Hello ──────────────────────────────
    HelloPayload hello{};
    memcpy(hello.HwidHash,     a->hwid,   32);
    memcpy(hello.LoaderHash,   a->loader, 32);
    memcpy(hello.PasswordHash, a->pwHash, 32);
    hello.BuildId   = 0x0001'0000;
    hello.Timestamp = (uint64_t)time(nullptr);
    strncpy_s(hello.Username,   sizeof(hello.Username),   state.username,   _TRUNCATE);
    strncpy_s(hello.LicenseKey, sizeof(hello.LicenseKey), state.licenseKey, _TRUNCATE);

    std::string authHex = HexStr(reinterpret_cast<uint8_t*>(&hello), sizeof(hello));
    std::string wsKey   = WsGenKey();

    std::string httpReq =
        std::string(_S("GET /auth HTTP/1.1\r\n")) +
        _S("Host: ") + state.serverHost + _S("\r\n") +
        _S("Upgrade: websocket\r\n") +
        _S("Connection: Upgrade\r\n") +
        _S("Sec-WebSocket-Key: ") + wsKey + _S("\r\n") +
        _S("Sec-WebSocket-Version: 13\r\n") +
        _S("x-sparky-key: VhPuLNayUPLTtOkOMoChbnaKHexOCetJaa4iXkLDF2s=\r\n") +
        _S("x-sparky-auth: ") + authHex + _S("\r\n\r\n");

    if (!NetSend(ss.sock, ss.ssl, httpReq.c_str(), (int)httpReq.size()))
    {
        state.AddLog("[ERR] Send WebSocket upgrade request failed");
        done(); return 1;
    }

    {
        char resp[2048]{};
        int received = 0;
        while (received < (int)sizeof(resp) - 1)
        {
            char c;
            if (!NetRecv(ss.sock, ss.ssl, &c, 1, 10000)) break;
            resp[received++] = c;
            if (received >= 4 && memcmp(resp + received - 4, "\r\n\r\n", 4) == 0) break;
        }
        if (strstr(resp, _S(" 101")) != nullptr)
        {
            ss.wsMode = true;
            state.AddLog("[INF] WebSocket upgrade accepted");
        }
        else if (strstr(resp, _S(" 200")) != nullptr)
        {
            ss.wsMode = false;
            state.AddLog("[WRN] Legacy server (no WebSocket upgrade)");
        }
        else
        {
            state.AddLog("[ERR] Cloud Armor / LB rejected the request");
            done(); return 1;
        }
    }

    // ── Phase 1d: Receive AuthOk / AuthFail ──────────────────────────────
    {
        MsgType t{}; std::vector<uint8_t> pay;
        if (!RecvMsg(ss, t, pay))
        {
            state.AddLog("[ERR] No auth response from server");
            done(); return 1;
        }
        if (t == MsgType::AuthFail)
        {
            state.AddLog("[ERR] Auth rejected by server");
            strncpy_s(state.loginError, sizeof(state.loginError),
                      "Authentication failed \xe2\x80\x94 check your credentials or license key.",
                      _TRUNCATE);
            done(); return 1;
        }
        if (t != MsgType::AuthOk || pay.size() < sizeof(AuthOkPayload))
        {
            state.AddLog("[ERR] Unexpected message during auth");
            done(); return 1;
        }
        const auto& aok = *reinterpret_cast<const AuthOkPayload*>(pay.data());
        memcpy(ss.token, aok.SessionToken, 16);
        ss.hdrKey = DeriveKey(ss.token, 0);
        ss.dllKey = DeriveKey(ss.token, 1);

        state.serverConnected = true;
        state.loggedIn        = true;
        state.loggingIn       = false;
        state.connecting      = false;
        state.loginError[0]   = '\0';
        state.AddLog("[INF] Authenticated — session active");
    }

    // ── Phase 2: Session loop ─────────────────────────────────────────────
    // Sends heartbeats every 25 s to keep the server session alive.
    // When state.dllRequested is set (user pressed Inject), sends RequestDll
    // and receives the DLL stream via ReceiveDll().
    int hbTicks = 0;
    static constexpr int HB_INTERVAL_TICKS = 25; // 25 × 1 s = 25 s

    while (true)
    {
        Sleep(1000);
        ++hbTicks;

        if (state.dllRequested)
        {
            state.dllRequested    = false;
            state.connecting      = true;  // show "Fetching…" spinner
            state.downloadProgress = 0.f;

            if (!SendMsg(ss, MsgType::RequestDll))
            {
                state.AddLog("[ERR] RequestDll send failed");
                state.connecting = false;
                break;
            }

            std::vector<uint8_t> dllBuf;
            bool ok = ReceiveDll(ss, dllBuf, state);
            state.connecting = false;

            if (!ok)
            {
                state.AddLog("[ERR] DLL receive failed — session closing");
                break;
            }

            if (!dllBuf.empty())
            {
                *a->dll            = std::move(dllBuf);
                state.dllSizeBytes = (uint32_t)a->dll->size();
                state.dllReady     = true;
                state.AddLog("[INF] DLL ready in RAM ("
                             + std::to_string(a->dll->size()) + " bytes)");
            }

            hbTicks = 0; // reset heartbeat timer after DLL exchange
        }

        if (hbTicks >= HB_INTERVAL_TICKS)
        {
            hbTicks = 0;
            HeartbeatPayload hb{};
            if (!SendMsg(ss, MsgType::Heartbeat, &hb, sizeof(hb))) break;
            MsgType t{}; std::vector<uint8_t> mp;
            if (!RecvMsg(ss, t, mp, 10000)) break;
        }
    }

    // ── Phase 3: Cleanup ─────────────────────────────────────────────────
    state.AddLog("[INF] Session closed");
    done();
    return 0;
}

// ---------------------------------------------------------------------------
// PingerThread — periodic HTTP health-check that sets state.serverConnected
// BEFORE the user logs in, so the status dot reflects server availability.
// While a session is active the SessionThread owns the connection; we skip
// pinging then to avoid redundant connections.
// ---------------------------------------------------------------------------
static DWORD WINAPI PingerThread(LPVOID p)
{
    struct Args { UIState* state; std::atomic<bool>* run; };
    auto* a    = static_cast<Args*>(p);
    UIState& state = *a->state;

    while (a->run->load())
    {
        // SessionThread owns serverConnected while a session is active
        if (state.loggedIn) { Sleep(3000); continue; }

        bool ok = false;

        WSADATA wsa{};
        WSAStartup(MAKEWORD(2,2), &wsa);

        const char* host = state.serverHost;
        char portStr[8];
        snprintf(portStr, sizeof(portStr), "%d", state.serverPort);

        addrinfo hints{}, *res = nullptr;
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        SOCKET sock = INVALID_SOCKET;
        SSL*   ssl  = nullptr;

        if (getaddrinfo(host, portStr, &hints, &res) == 0 && res)
        {
            sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            if (sock != INVALID_SOCKET)
            {
                DWORD to = 5000;
                setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));
                setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&to, sizeof(to));

                if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == 0)
                {
                    if (g_loaderSslCtx)
                    {
                        ssl = SSL_new(g_loaderSslCtx);
                        SSL_set_fd(ssl, (int)sock);
                        SSL_set_tlsext_host_name(ssl, host);
                        if (SSL_connect(ssl) <= 0) { SSL_free(ssl); ssl = nullptr; }
                    }

                    if (!g_loaderSslCtx || ssl)
                    {
                        char req[512];
                        snprintf(req, sizeof(req),
                            "GET / HTTP/1.1\r\n"
                            "Host: %s\r\n"
                            "x-sparky-key: VhPuLNayUPLTtOkOMoChbnaKHexOCetJaa4iXkLDF2s=\r\n"
                            "Connection: close\r\n\r\n",
                            host);
                        if (NetSend(sock, ssl, req, (int)strlen(req)))
                        {
                            char resp[256]{};
                            NetRecv(sock, ssl, resp, sizeof(resp) - 1, 5000);
                            ok = strstr(resp, " 200") != nullptr;
                        }
                    }
                }
            }
            freeaddrinfo(res);
        }

        if (ssl)  { SSL_shutdown(ssl); SSL_free(ssl); }
        if (sock != INVALID_SOCKET) closesocket(sock);
        WSACleanup();

        if (!state.loggedIn)
            state.serverConnected = ok;

        // Wait ~15 s, waking every second to check for shutdown or login
        for (int i = 0; i < 15 && a->run->load() && !state.loggedIn; ++i)
            Sleep(1000);
    }
    delete a;
    return 0;
}

// ---------------------------------------------------------------------------
// ProcessWatcher — background thread, no heap allocs
// ---------------------------------------------------------------------------
static void ProcessWatcher(UIState& state, std::atomic<bool>& running,
                           const std::vector<uint8_t>& dllInRam)
{
    while (running.load())
    {
        DWORD pid = FindProcessByName(state.processName);
        state.processFound = (pid != 0);
        state.targetPid    = pid;

        // Keep dllReady accurate: true if server RAM buffer OR local file is present.
        if (!state.dllReady)
        {
            if (!dllInRam.empty())
            {
                state.dllReady     = true;
                state.dllSizeBytes = static_cast<uint32_t>(dllInRam.size());
            }
            else if (strlen(state.dllPath) > 0)
            {
                DWORD attr = GetFileAttributesA(state.dllPath);
                state.dllReady = (attr != INVALID_FILE_ATTRIBUTES);
            }
        }

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
    strcpy_s(state.serverHost, sizeof(state.serverHost), _S("35.206.181.36"));
    state.AddLog("[INF] Sparky ready");

    uint8_t hwidHash[32]{};
    if (!GetHwidHash(hwidHash))
        state.AddLog("[WRN] HWID hash failed");

    uint8_t loaderHash[32]{};
    if (!GetLoaderHash(loaderHash))
        state.AddLog("[WRN] Loader hash failed — integrity check will reject");

    std::vector<uint8_t> dllInRam;

    std::atomic<bool> watcherRunning = true;
    struct WatchArgs { UIState* st; std::atomic<bool>* run; std::vector<uint8_t>* dll; };
    HANDLE hWatcher = CreateThread(nullptr, 0,
        [](LPVOID p) -> DWORD {
            auto* a = static_cast<WatchArgs*>(p);
            ProcessWatcher(*a->st, *a->run, *a->dll);
            delete a;
            return 0;
        },
        new WatchArgs{&state, &watcherRunning, &dllInRam},
        0, nullptr);

    // Start server connectivity pinger — sets state.serverConnected before login
    struct PingArgs { UIState* state; std::atomic<bool>* run; };
    CreateThread(nullptr, 0, PingerThread, new PingArgs{&state, &watcherRunning}, 0, nullptr);

    auto onConnect = [&]() {
        if (state.connecting) return;  // already in flight

        // If already authenticated, just request the DLL (user pressed Inject
        // while logged in but before the DLL was fetched).
        if (state.loggedIn && state.serverConnected)
        {
            if (!state.dllReady && !state.dllRequested)
            {
                state.dllRequested = true;
                state.connecting   = true;
            }
            return;
        }

        // New session — hash password on the UI thread, zero plaintext immediately.
        uint8_t passwordHash[32]{};
        if (state.password[0] != '\0')
        {
            HashBytes(state.password, strlen(state.password), passwordHash);
            SecureZeroMemory(state.password,     sizeof(state.password));
            SecureZeroMemory(state.passwordConf, sizeof(state.passwordConf));
        }

        // serverConnected is left as-is; PingerThread keeps it accurate.
        state.dllReady         = false;
        state.dllSizeBytes     = 0;
        state.downloadProgress = 0.f;
        state.connecting       = true;
        state.loggingIn        = true;
        // If a preset was already chosen, queue the DLL request for right after login.
        state.dllRequested     = state.autoInjectPending;
        dllInRam.clear();

        state.AddLog("[INF] Connecting...");

        auto* args = new SessionArgs{};
        args->state = &state;
        args->dll   = &dllInRam;
        memcpy(args->hwid,   hwidHash,     32);
        memcpy(args->loader, loaderHash,   32);
        memcpy(args->pwHash, passwordHash, 32);
        SecureZeroMemory(passwordHash, 32);

        CreateThread(nullptr, 0, SessionThread, args, 0, nullptr);
    };

    auto onInject = [&]() {
        if (state.injecting || state.injected) return;

        // Resolve target PID: custom (Tab 2) takes priority over ProcessWatcher.
        DWORD targetPid = (state.customTargetPid != 0)
                          ? state.customTargetPid
                          : state.targetPid;

        if (targetPid == 0)
        { state.AddLog("[ERR] Process not found — is the target running?"); return; }

        // Dev mode fallback: read DLL from disk if server hasn't delivered one yet.
        if (dllInRam.empty() && strlen(state.dllPath) > 0)
        {
            HANDLE hF = CreateFileA(state.dllPath, GENERIC_READ, FILE_SHARE_READ,
                                     nullptr, OPEN_EXISTING, 0, nullptr);
            if (hF != INVALID_HANDLE_VALUE)
            {
                LARGE_INTEGER sz{};
                GetFileSizeEx(hF, &sz);
                dllInRam.resize(static_cast<size_t>(sz.QuadPart));
                DWORD r{};
                ReadFile(hF, dllInRam.data(), static_cast<DWORD>(sz.QuadPart), &r, nullptr);
                CloseHandle(hF);
                state.dllSizeBytes = static_cast<uint32_t>(dllInRam.size());
                state.dllReady     = true;
                state.AddLog("[WRN] Using local DLL (dev mode — server DLL not loaded)");
            }
        }

        if (dllInRam.empty())
        { state.AddLog("[ERR] No DLL available — connect to server first or set a local DLL path"); return; }

        state.injecting = true;
        state.AddLog("[INF] Injecting " + std::to_string(dllInRam.size())
                     + " bytes into PID " + std::to_string(targetPid) + "...");

        struct InjectArgs { UIState* st; DWORD pid; std::vector<uint8_t> dll; };
        auto* args = new InjectArgs{ &state, targetPid, dllInRam };

        CreateThread(nullptr, 0,
            [](LPVOID p) -> DWORD {
                auto* a = static_cast<InjectArgs*>(p);
                HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, a->pid);
                if (!hProc)
                {
                    a->st->AddLog("[ERR] OpenProcess failed (error "
                                  + std::to_string(GetLastError()) + ")");
                    a->st->injecting = false;
                    delete a; return 1;
                }

                bool ok = ManualMapDll(hProc, a->dll);
                CloseHandle(hProc);

                a->st->injecting = false;
                a->st->injected  = ok;
                a->st->AddLog(ok ? "[INF] Injection successful — DLL running in target"
                                 : "[ERR] Injection failed");
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
