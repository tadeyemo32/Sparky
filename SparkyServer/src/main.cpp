// SparkyServer — auth + license DB + heartbeat-gated DLL streaming
#ifdef _WIN32
#  define _WINSOCK_DEPRECATED_NO_WARNINGS
#  include <WinSock2.h>
#  include <WS2tcpip.h>
#  include <Windows.h>
#  include <wincrypt.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>   // TCP_NODELAY
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <sys/stat.h>
#  include <sys/wait.h>      // waitpid, fork
#  include <dirent.h>        // opendir/readdir for backup rotation
#  include <signal.h>
#  include <openssl/rand.h>
#endif
#include <cstdint>
#include <cstring>
#include <ctime>
#include <chrono>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>
#include <format>
#include <algorithm>         // std::sort for backup rotation
#include <unordered_set>     // g_activeHwids

// OpenSSL TLS — optional at runtime.
// Place sparky.crt + sparky.key next to the binary to enable TLS.
// If absent the server runs in plaintext mode (dev mode).
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>

// OpenSSL import libs are linked via CMake (OpenSSL::SSL, OpenSSL::Crypto)

#include "../include/Database.h"
#include "../include/LicenseManager.h"
#include "../include/RateLimiter.h"
#include "../include/KeyVault.h"
#include "../include/XorStr.h"
#include "../include/SecureString.h"
#include "../../SparkyLoader/user/include/Protocol.h"
#include "../../SparkyLoader/user/include/TlsLayer.h"
#include "../../SparkyLoader/user/include/WebSocket.h"

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
static constexpr uint16_t DEFAULT_LISTEN_PORT    = 7777;
static uint16_t           LISTEN_PORT            = DEFAULT_LISTEN_PORT;
static constexpr uint32_t CURRENT_BUILD          = 0x0001'0000;
static constexpr uint32_t CHUNK_SIZE             = 4096;
static constexpr int      MAX_CONCURRENT_SESSIONS = 500;  // hard cap on auth'd sessions
static constexpr int      BACKUP_KEEP_COUNT       = 7;    // keep last N backup files

// How many DLL chunks to send between mandatory client heartbeats.
// At 4 KB/chunk, 8 chunks = 32 KB per heartbeat interval.
// If the client goes silent for HEARTBEAT_DEADLINE_MS after its batch,
// the server drops the connection mid-stream.
static constexpr uint32_t CHUNKS_PER_HEARTBEAT = 8;

static const char* DLL_FILE    = XS("SparkyCore.dll");
static const char* CONFIG_FILE = XS("config.bin");
static const char* CERT_FILE   = XS("sparky.crt");
static const char* KEY_FILE    = XS("sparky.key");
static constexpr bool ENABLE_ADMIN_CONSOLE = true;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static Database        g_db;
static LicenseManager* g_lm = nullptr;
static std::mutex      g_dbMu;

// Per-endpoint rate limiters — separate buckets prevent flooding one path
// from consuming quota on another.
static RateLimiter g_rl_auth(5, 10, 60);    // /api/auth/* — 5 soft / 10 hard per 60 s
static RateLimiter g_rl_api(20, 50, 60);    // /api/admin|owner/* — 20 soft / 50 hard
static RateLimiter g_rl_loader(5, 10, 300); // loader binary protocol — 5 per 5 min

static std::vector<uint8_t> g_dllBytes;
static std::vector<uint8_t> g_cfgBytes;    // config.bin loaded once at startup
static std::atomic<int>     g_activeSessions{0};
static std::atomic<bool>    g_running{true};

// TLS context — nullptr means plaintext (dev mode, no cert/key files found)
static SSL_CTX* g_sslCtx = nullptr;

// HWID pepper — server-side secret mixed into the HWID hash before DB storage.
// Prevents an attacker with DB read access from reversing HWIDs.
// Load: set SPARKY_HWID_PEPPER=<64 hex chars> before starting the server.
// Generate one: openssl rand -hex 32
static uint8_t g_hwidPepper[32]{};
static bool    g_pepperSet = false;

// SecureString globals — contents are XOR'd with a per-instance random key so
// secrets never sit as plaintext in a heap allocation.  Get() decrypts transiently.
static SecureString g_sparkyKey;

// Connection string stored globally for RunBackup()
static SecureString g_connstr;

// Server start time — used for uptime reporting in /api/owner/metrics
static int64_t g_startTime = 0;

// Allowed web origin — set SPARKY_ALLOWED_ORIGIN to your Vercel domain
// (e.g. https://sparky.vercel.app).  When set, all /api/ requests must carry
// an Origin header matching this value; CORS responses use the specific origin
// instead of *.  If unset, origin checking is skipped (dev mode).
// Not stored as SecureString — the origin is a public domain, not a secret,
// and it is compared on every request in the hot path.
static std::string g_allowedOrigin;

// Resend API — set RESEND_API_KEY to enable transactional email (OTP, forgot-password).
// Set RESEND_FROM_EMAIL to your verified sender address (e.g. "Sparky <noreply@yourdomain.com>").
static SecureString g_resendKey;
static SecureString g_resendFrom;

// Loader attestation key — 32 raw bytes loaded from SPARKY_ATTEST_KEY (64 hex chars).
// When set, the loader sends HMAC-SHA256(SHA-256(binary), attest_key) instead of the
// raw binary hash.  The server verifies the HMAC against every entry in trusted_hashes.
// Without this key an attacker cannot compute a valid HMAC for any binary they control.
static uint8_t g_attestKey[32]{};
static bool    g_attestKeySet = false;

// Proxy secret — shared secret between Vercel serverless proxy and this server.
// The proxy injects it as X-Proxy-Secret; the server rejects web API requests that
// lack it. Prevents anyone who discovers the backend IP from calling /api/* directly.
// Set SPARKY_PROXY_SECRET to a 32+ byte random hex string on both sides.
static SecureString g_proxySecret;

// Per-HWID concurrent session guard.
// Prevents one user from opening multiple simultaneous sessions.
// Protected by g_hwidMu (separate from g_dbMu to avoid deadlocks).
static std::unordered_set<std::string> g_activeHwids;
static std::mutex                       g_hwidMu;

// Per-IP concurrent connection cap — prevents single-IP exhaustion even
// within rate-limit windows.  Max 20 simultaneous sockets per IP.
static std::unordered_map<std::string, int> g_connCount;
static std::mutex                            g_connMu;

#ifdef _WIN32
static BOOL WINAPI CtrlHandler(DWORD)
{
    std::cout << "[S] Shutdown signal received — stopping accept loop...\n";
    g_running = false;
    return TRUE;
}
#else
static void PosixSignalHandler(int)
{
    std::cout << "[S] Shutdown signal received — stopping accept loop...\n";
    g_running = false;
}
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
#include <random>

static std::string GenerateRandomString(size_t length)
{
    const std::string chars = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<> distribution(0, (int)chars.size() - 1);
    std::string result;
    for (size_t i = 0; i < length; ++i) {
        result += chars[distribution(generator)];
    }
    return result;
}

// HexStr is now inline in Protocol.h

static std::vector<uint8_t> ReadFileFull(const char* path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    std::vector<uint8_t> buf((size_t)f.tellg()); f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)buf.size());
    return buf;
}

// ---------------------------------------------------------------------------
// PepperHwid — applies the server-side HWID pepper.
//
// The loader sends SHA-256(MachineGuid). If g_pepperSet is true, the server
// computes SHA-256(raw_hwid_bytes || g_hwidPepper) before storing or looking
// up the HWID. This means:
//   - The stored HWID hash can never be reversed back to the machine GUID
//     even if an attacker gets a full DB dump.
//   - The pepper must never change, or all existing HWID bindings break.
//   - Use --gen-token to generate a random pepper; store it in
//     SPARKY_HWID_PEPPER and back it up separately from the database.
// ---------------------------------------------------------------------------
static std::string PepperHwid(const std::string& hwid)
{
    if (!g_pepperSet || hwid.size() != 64) return hwid;

    // Decode 64 hex chars → 32 raw bytes
    uint8_t raw[32]{};
    auto h2 = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
        if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
        return 0;
    };
    for (int i = 0; i < 32; ++i)
        raw[i] = (h2(hwid[i*2]) << 4) | h2(hwid[i*2+1]);

    // SHA-256(raw_hwid || pepper)
    uint8_t digest[32]{};
    bool ok = false;

#ifdef _WIN32
    HCRYPTPROV hProv{};
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return hwid; // fallback to unpepered on CryptAPI failure

    HCRYPTHASH hHash{};
    if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
    {
        if (CryptHashData(hHash, raw, 32, 0)
            && CryptHashData(hHash, g_hwidPepper, 32, 0))
        {
            DWORD hl = 32;
            ok = CryptGetHashParam(hHash, HP_HASHVAL, digest, &hl, 0) == TRUE;
        }
        CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);
#else
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    ok = ctx
        && EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr)
        && EVP_DigestUpdate(ctx, raw, 32)
        && EVP_DigestUpdate(ctx, g_hwidPepper, 32);
    if (ok) { unsigned int dl = 32; ok = EVP_DigestFinal_ex(ctx, digest, &dl); }
    if (ctx) EVP_MD_CTX_free(ctx);
#endif

    return ok ? HexStr(digest, 32) : hwid;
}

// ---------------------------------------------------------------------------
// RotateBackups — keep only the BACKUP_KEEP_COUNT most recent .sql files
// in the backups/ directory.  Called after each successful pg_dump.
// ---------------------------------------------------------------------------
static void RotateBackups()
{
#ifdef _WIN32
    WIN32_FIND_DATAA fd{};
    HANDLE hFind = FindFirstFileA(XS("backups\\*.sql"), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    std::vector<std::string> files;
    do { files.push_back(fd.cFileName); } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    std::sort(files.begin(), files.end());
    while ((int)files.size() > BACKUP_KEEP_COUNT)
    {
        DeleteFileA((std::string(XS("backups\\")) + files.front()).c_str());
        files.erase(files.begin());
    }
#else
    DIR* dir = opendir(XS("backups"));
    if (!dir) return;
    std::vector<std::string> files;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr)
    {
        std::string n = ent->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, XS(".sql")) == 0)
            files.push_back(n);
    }
    closedir(dir);
    std::sort(files.begin(), files.end());
    while ((int)files.size() > BACKUP_KEEP_COUNT)
    {
        ::remove((std::string(XS("backups/")) + files.front()).c_str());
        files.erase(files.begin());
    }
#endif
}

// ---------------------------------------------------------------------------
// RunBackup — dump the PostgreSQL database via pg_dump.
// Writes to backups/sparky_YYYYMMDD_HHMMSS.sql next to the binary.
// pg_dump must be in PATH (it ships with the PostgreSQL client tools).
// Called from MaintenanceThread every 6 hours.
//
// On Linux: uses fork()+execvp() to avoid shell injection through the
// connection string.  On Windows: uses CreateProcessA (already safe).
// ---------------------------------------------------------------------------
static void RunBackup()
{
    if (g_connstr.empty()) return;

    // Ensure backups/ directory exists (no-op if already there)
#ifdef _WIN32
    CreateDirectoryA(XS("backups"), nullptr);
#else
    mkdir(XS("backups"), 0755);
#endif

    // Build timestamp filename
    time_t now = time(nullptr);
    tm tbuf{};
#ifdef _WIN32
    localtime_s(&tbuf, &now);
#else
    localtime_r(&now, &tbuf);
#endif
    tm* t = &tbuf;
    char fname[80]{};
    snprintf(fname, sizeof(fname),
             XS("backups/sparky_%04d%02d%02d_%02d%02d%02d.sql"),
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

#ifdef _WIN32
    // Build command for CreateProcessA (no shell involved — safe)
    std::string _cs = g_connstr.Get();
    std::string cmd = std::string(XS("pg_dump -d \"")) + _cs + XS("\" -f \"") + fname + XS("\"");
    OPENSSL_cleanse(_cs.data(), _cs.size());

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()),
                        nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi))
    {
        std::cout << "[S] RunBackup: CreateProcess failed — is pg_dump in PATH?\n";
        return;
    }

    WaitForSingleObject(pi.hProcess, 120000); // wait up to 2 min
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode == 0)
    {
        std::cout << std::format("[S] DB backup → {}\n", fname);
        RotateBackups();
    }
    else
        std::cout << std::format("[S] pg_dump exited {} — check PATH / credentials\n", exitCode);
#else
    // Use fork()+execvp() — the connection string is passed as a direct argv
    // element (no shell), so special characters cannot cause injection.
    pid_t pid = fork();
    if (pid < 0)
    {
        std::cout << "[S] RunBackup: fork() failed\n";
        return;
    }
    if (pid == 0)
    {
        // Child: exec pg_dump directly — no shell, no injection
        std::string _cs2 = g_connstr.Get();
        const char* args[] = { "pg_dump", "-d", _cs2.c_str(), "-f", fname, nullptr };
        execvp("pg_dump", const_cast<char* const*>(args));
        _exit(127); // exec failed
    }

    // Parent: poll waitpid up to 120 seconds
    int wstatus = 0;
    for (int i = 0; i < 120; ++i)
    {
        pid_t w = waitpid(pid, &wstatus, WNOHANG);
        if (w == pid)
        {
            int ec = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
            if (ec == 0)
            {
                std::cout << std::format("[S] DB backup → {}\n", fname);
                RotateBackups();
            }
            else
                std::cout << std::format("[S] pg_dump exited {} — check PATH / credentials\n", ec);
            return;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    // Timeout: kill the child
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    std::cout << "[S] RunBackup: pg_dump timed out (120 s) — killed\n";
#endif
}

// ---------------------------------------------------------------------------
// Per-client session state
// ---------------------------------------------------------------------------
struct ClientSession
{
#ifdef _WIN32
    SOCKET      sock;
#else
    int         sock;
#endif
    SSL*        ssl      = nullptr;  // nullptr = plaintext dev mode
    uint64_t    hdrKey   = 0;
    uint64_t    dllKey   = 0;
    uint8_t     token[16]{};
    std::string hwid;
    std::string tokenHex;
    std::string loaderHash; // hex of loader SHA-256 from Hello
    std::string ip;         // remote IP, set on accept
    bool        wsMode     = false; // true after WebSocket upgrade handshake
    bool        keepAlive  = false; // true when client sent Connection: keep-alive
};

static bool SendMsg(ClientSession& s, MsgType t,
                    const void* pay = nullptr, uint16_t len = 0)
{
    MsgHeader h{}; h.Magic=PROTO_MAGIC; h.Version=PROTO_VERSION; h.Type=t; h.Length=len;
    std::vector<uint8_t> buf(len);
    if (pay && len) memcpy(buf.data(), pay, len);
    if (s.hdrKey && !buf.empty()) XorStream(buf.data(), (uint32_t)buf.size(), s.hdrKey);
    uint32_t crc = Crc32((uint8_t*)&h, sizeof(h));
    if (!buf.empty()) crc ^= Crc32(buf.data(), (uint32_t)buf.size());

    if (s.wsMode) {
        // Pack the entire message into one buffer and send as a single WS frame.
        // The LB forwards WebSocket frames transparently (RFC 6455 passthrough).
        std::vector<uint8_t> msg(sizeof(h) + buf.size() + 4);
        memcpy(msg.data(), &h, sizeof(h));
        if (!buf.empty()) memcpy(msg.data() + sizeof(h), buf.data(), buf.size());
        memcpy(msg.data() + sizeof(h) + buf.size(), &crc, 4);
        return WsSendFrame(s.sock, s.ssl, msg.data(), msg.size());
    }

    return NetSend(s.sock, s.ssl, &h, sizeof(h))
        && (buf.empty() || NetSend(s.sock, s.ssl, buf.data(), (int)buf.size()))
        && NetSend(s.sock, s.ssl, &crc, 4);
}

static bool RecvMsg(ClientSession& s, MsgType& t,
                    std::vector<uint8_t>& pay, unsigned int ms = 10000)
{
    if (s.wsMode) {
        std::vector<uint8_t> frame;
        if (!WsRecvFrame(s.sock, s.ssl, frame, ms)) return false;
        if (frame.size() < sizeof(MsgHeader) + 4) return false;
        MsgHeader h{};
        memcpy(&h, frame.data(), sizeof(h));
        if (h.Magic != PROTO_MAGIC || h.Version != PROTO_VERSION) return false;
        if (sizeof(MsgHeader) + h.Length + 4 > frame.size()) return false;
        pay.resize(h.Length);
        if (h.Length) memcpy(pay.data(), frame.data() + sizeof(h), h.Length);
        uint32_t rc{};
        memcpy(&rc, frame.data() + sizeof(h) + h.Length, 4);
        uint32_t lc = Crc32((uint8_t*)&h, sizeof(h));
        if (!pay.empty()) lc ^= Crc32(pay.data(), (uint32_t)pay.size());
        if (lc != rc) return false;
        if (s.hdrKey && !pay.empty()) XorStream(pay.data(), (uint32_t)pay.size(), s.hdrKey);
        t = h.Type; return true;
    }

    MsgHeader h{};
    if (!NetRecv(s.sock, s.ssl, &h, sizeof(h), ms)) return false;
    if (h.Magic != PROTO_MAGIC || h.Version != PROTO_VERSION) return false;
    pay.resize(h.Length);
    if (h.Length && !NetRecv(s.sock, s.ssl, pay.data(), h.Length, ms)) return false;
    uint32_t rc{}; if (!NetRecv(s.sock, s.ssl, &rc, 4, ms)) return false;
    uint32_t lc = Crc32((uint8_t*)&h, sizeof(h));
    if (!pay.empty()) lc ^= Crc32(pay.data(), (uint32_t)pay.size());
    if (lc != rc) return false;
    if (s.hdrKey && !pay.empty()) XorStream(pay.data(), (uint32_t)pay.size(), s.hdrKey);
    t = h.Type; return true;
}

// ---------------------------------------------------------------------------
// Web API — REST endpoints for the React site (SparkySite)
//
// All routes live under /api/. Authentication is via Bearer token stored in
// web_sessions.  Access control: user < admin < owner.
//
// CORS: all responses include Access-Control-Allow-Origin: *  so the site
// can be served from any origin.  The SPARKY_KEY header still gates access.
// ---------------------------------------------------------------------------

// ── JSON helpers ─────────────────────────────────────────────────────────────

static std::string JEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += (char)c;
    }
    return out;
}

// Minimal JSON object key:value builders (value already JSON-encoded)
static std::string JField(const std::string& k, const std::string& jsonVal)
{ return "\"" + k + "\":" + jsonVal; }
static std::string JStr(const std::string& k, const std::string& v)
{ return JField(k, "\"" + JEscape(v) + "\""); }
static std::string JNum(const std::string& k, int64_t v)
{ return JField(k, std::to_string(v)); }
static std::string JBool(const std::string& k, bool v)
{ return JField(k, v ? "true" : "false"); }

// Extract a string value from a flat JSON object body.
// e.g. ParseJsonStr(body, "username") → "alice"
static std::string ParseJsonStr(const std::string& body, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return "";
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return "";
    ++p; // skip ':'
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
    if (p >= body.size() || body[p] != '"') return "";
    ++p; // skip opening quote
    std::string out;
    while (p < body.size() && body[p] != '"') {
        if (body[p] == '\\') return "";  // reject backslash — Sparky values are plain
        out += body[p++];
    }
    return out;
}

static int ParseJsonInt(const std::string& body, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return 0;
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return 0;
    ++p;
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
    if (p >= body.size()) return 0;
    try { return std::stoi(body.substr(p)); } catch (...) { return 0; }
}

// ── HTTP response helpers ─────────────────────────────────────────────────────

static void SendHttp(ClientSession& s, int code, const std::string& ct, const std::string& body)
{
    const char* statusMsg = "OK";
    switch (code) {
        case 201: statusMsg = "Created"; break;
        case 204: statusMsg = "No Content"; break;
        case 400: statusMsg = "Bad Request"; break;
        case 401: statusMsg = "Unauthorized"; break;
        case 403: statusMsg = "Forbidden"; break;
        case 404: statusMsg = "Not Found"; break;
        case 409: statusMsg = "Conflict"; break;
        case 500: statusMsg = "Internal Server Error"; break;
        default:  break;
    }
    // Use the specific allowed origin in CORS headers when configured;
    // fall back to * only in dev mode (no restriction set).
    const std::string& corsOrigin = g_allowedOrigin.empty() ? std::string("*") : g_allowedOrigin;
    std::string resp =
        "HTTP/1.1 " + std::to_string(code) + " " + statusMsg + "\r\n"
        "Access-Control-Allow-Origin: " + corsOrigin + "\r\n"
        "Vary: Origin\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization, x-sparky-key\r\n"
        "Content-Type: " + ct + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n" +
        (s.keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n") +
        "\r\n" + body;
    NetSend(s.sock, s.ssl, resp.c_str(), (int)resp.size());
}

static void SendJson(ClientSession& s, int code, const std::string& json)
{ SendHttp(s, code, "application/json", json); }

static void SendApiError(ClientSession& s, int code, const std::string& msg)
{ SendJson(s, code, "{\"error\":\"" + JEscape(msg) + "\"}"); }

// ── Token generation ──────────────────────────────────────────────────────────

static std::string MakeWebToken()
{
    uint8_t raw[32]{};
#ifdef _WIN32
    HCRYPTPROV hp{};
    if (CryptAcquireContextW(&hp, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(hp, sizeof(raw), raw);
        CryptReleaseContext(hp, 0);
    }
#else
    RAND_bytes(raw, sizeof(raw));
#endif
    return HexStr(raw, sizeof(raw));
}

// ── OTP generation ────────────────────────────────────────────────────────────

// Returns a 6-digit zero-padded numeric OTP string.
static std::string MakeOtp()
{
    uint8_t raw[4]{};
    RAND_bytes(raw, sizeof(raw));
    uint32_t n = (uint32_t)raw[0]
               | ((uint32_t)raw[1] << 8)
               | ((uint32_t)raw[2] << 16)
               | ((uint32_t)raw[3] << 24);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06u", (unsigned)(n % 1000000u));
    return std::string(buf);
}

// ── Transactional email via Resend API ────────────────────────────────────────

// Sends an email through the Resend REST API over raw HTTPS (no libcurl required).
// Returns true if the API returns a 2xx status; logs failures to stderr.
// If RESEND_API_KEY is not set the email is silently skipped and false is returned.
static bool SendResendEmail(const std::string& to,
                             const std::string& subject,
                             const std::string& htmlBody)
{
    if (g_resendKey.empty()) {
        std::cerr << "[Email] RESEND_API_KEY not set — email to " << to << " skipped\n";
        return false;
    }

    std::string _rk   = g_resendKey.Get();
    std::string _from = g_resendFrom.empty()
                      ? std::string(XS("Sparky <noreply@spxrky.com>"))
                      : g_resendFrom.Get();
    std::string from  = _from;

    std::string payload = "{"
        "\"from\":\""    + JEscape(from)     + "\","
        "\"to\":[\""     + JEscape(to)       + "\"],"
        "\"subject\":\"" + JEscape(subject)  + "\","
        "\"html\":\""    + JEscape(htmlBody) + "\""
        "}";

    // Resolve api.resend.com
    struct addrinfo hints{}, *addrRes = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("api.resend.com", "443", &hints, &addrRes) != 0 || !addrRes) {
        std::cerr << "[Email] DNS failed for api.resend.com\n";
        return false;
    }

#ifdef _WIN32
    SOCKET sock = socket(addrRes->ai_family, addrRes->ai_socktype, addrRes->ai_protocol);
    bool connected = (sock != INVALID_SOCKET) &&
                     connect(sock, addrRes->ai_addr, (int)addrRes->ai_addrlen) == 0;
#else
    int sock = socket(addrRes->ai_family, addrRes->ai_socktype, addrRes->ai_protocol);
    bool connected = (sock >= 0) &&
                     connect(sock, addrRes->ai_addr, (socklen_t)addrRes->ai_addrlen) == 0;
#endif
    freeaddrinfo(addrRes);

    bool ok = false;
    if (connected) {
        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (ctx) {
            SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
            SSL_CTX_set_default_verify_paths(ctx);
            SSL* ssl = SSL_new(ctx);
            if (ssl) {
                SSL_set_fd(ssl, (int)sock);
                SSL_set_tlsext_host_name(ssl, "api.resend.com");
                if (SSL_connect(ssl) == 1) {
                    std::string req =
                        "POST /emails HTTP/1.1\r\n"
                        "Host: api.resend.com\r\n"
                        "Authorization: Bearer " + _rk + "\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: " + std::to_string(payload.size()) + "\r\n"
                        "Connection: close\r\n"
                        "\r\n" + payload;
                    OPENSSL_cleanse(_rk.data(), _rk.size());
                    OPENSSL_cleanse(_from.data(), _from.size());
                    SSL_write(ssl, req.c_str(), (int)req.size());
                    OPENSSL_cleanse(req.data(), req.size());
                    char buf[2048]{};
                    SSL_read(ssl, buf, sizeof(buf) - 1);
                    ok = std::strstr(buf, "HTTP/1.1 2") != nullptr;
                    if (!ok)
                        std::cerr << "[Email] Resend error: "
                                  << std::string(buf, std::min((size_t)200, std::strlen(buf)))
                                  << "\n";
                }
                SSL_free(ssl);
            }
            SSL_CTX_free(ctx);
        }
    }
#ifdef _WIN32
    closesocket(sock);
#else
    if (sock >= 0) close(sock);
#endif
    return ok;
}

// ── Bearer token extraction ───────────────────────────────────────────────────

static std::string ExtractBearerToken(const std::string& req)
{
    // Case-insensitive search for "authorization:" header
    std::string lr = req;
    for (auto& c : lr) c = (char)std::tolower((unsigned char)c);
    size_t p = lr.find("authorization:");
    if (p == std::string::npos) return "";
    p += 14;
    while (p < lr.size() && (lr[p] == ' ' || lr[p] == '\t')) ++p;
    if (lr.compare(p, 7, "bearer ") != 0) return "";
    p += 7;
    size_t end = p;
    while (end < req.size() && req[end] != '\r' && req[end] != '\n' && req[end] != ' ') ++end;
    return req.substr(p, end - p);
}

// ── Web auth info ─────────────────────────────────────────────────────────────

struct WebAuthInfo
{
    bool        ok       = false;
    std::string username;
    std::string role;    // "user", "admin", "owner"
    std::string hwid;
};

// Validates a web Bearer token and returns the authenticated user's info.
// Caller must NOT hold g_dbMu when calling this.
static WebAuthInfo ValidateWebToken(const std::string& token)
{
    if (token.empty()) return {};

    std::optional<WebSessionRow> ws;
    {
        std::lock_guard lk(g_dbMu);
        ws = g_db.GetWebSession(token);
        if (!ws) return {};
        if (ws->expires_at < (int64_t)time(nullptr)) {
            g_db.DeleteWebSession(token);
            return {};
        }
    }

    // Look up hwid for this username
    std::string hwid;
    {
        std::lock_guard lk(g_dbMu);
        auto user = g_db.GetUserByUsername(ws->username);
        if (user) hwid = user->hwid_hash;
    }

    WebAuthInfo info;
    info.ok       = true;
    info.username = ws->username;
    info.role     = ws->role;
    info.hwid     = hwid;
    return info;
}

// ── Route dispatcher ──────────────────────────────────────────────────────────

static void HandleWebApi(ClientSession& s,
                          const std::string& method,
                          const std::string& path,
                          const std::string& req,       // full headers
                          const std::string& body)
{
    // ── Origin restriction ────────────────────────────────────────────────────
    // When SPARKY_ALLOWED_ORIGIN is set, reject requests whose Origin header
    // doesn't match.  This restricts the REST API to the configured Vercel
    // deployment and prevents abuse from other origins.
    if (!g_allowedOrigin.empty())
    {
        std::string lr = req;
        for (auto& c : lr) c = (char)std::tolower((unsigned char)c);
        size_t op = lr.find("origin:");
        if (op != std::string::npos)
        {
            // Only block browser requests whose Origin header doesn't match.
            // Server-to-server requests (Vercel proxy, health checks) send no
            // Origin header and are authenticated solely by X-Proxy-Secret.
            size_t vs = op + 7;
            while (vs < req.size() && (req[vs] == ' ' || req[vs] == '\t')) ++vs;
            size_t ve = req.find('\r', vs);
            if (ve == std::string::npos) ve = req.size();
            while (ve > vs && (req[ve-1] == ' ' || req[ve-1] == '\t')) --ve;
            std::string origin = req.substr(vs, ve - vs);
            if (origin != g_allowedOrigin)
            {
                SendApiError(s, 403, "Origin not permitted");
                return;
            }
        }
    }

    // ── Proxy secret check ────────────────────────────────────────────────────
    // When SPARKY_PROXY_SECRET is set, every /api/* request must carry the
    // matching X-Proxy-Secret header (injected server-side by the Vercel proxy).
    // This prevents direct access to the backend even if the IP is discovered.
    if (!g_proxySecret.empty())
    {
        std::string lreq = req;
        for (auto& c : lreq) c = (char)std::tolower((unsigned char)c);
        std::string sent;
        size_t pp = lreq.find("x-proxy-secret:");
        if (pp != std::string::npos)
        {
            size_t vs = pp + 15;
            while (vs < req.size() && (req[vs] == ' ' || req[vs] == '\t')) ++vs;
            size_t ve = req.find('\r', vs);
            if (ve == std::string::npos) ve = req.size();
            while (ve > vs && (req[ve-1] == ' ' || req[ve-1] == '\t')) --ve;
            sent = req.substr(vs, ve - vs);
        }
        std::string _ps = g_proxySecret.Get();
        bool secretOk = sent.size() == _ps.size() &&
                        CRYPTO_memcmp(sent.data(), _ps.data(), _ps.size()) == 0;
        OPENSSL_cleanse(_ps.data(), _ps.size());
        if (!secretOk)
        {
            SendApiError(s, 403, "Forbidden");
            return;
        }
    }

    // ── Per-endpoint rate limiting ────────────────────────────────────────────
    if (!s.ip.empty())
    {
        if (path.rfind("/api/auth/", 0) == 0)
        {
            if (!g_rl_auth.Allow(s.ip))
            { SendApiError(s, 429, "Too Many Requests"); return; }
        }
        else
        {
            if (!g_rl_api.Allow(s.ip))
            { SendApiError(s, 429, "Too Many Requests"); return; }
        }
    }

    // ── POST /api/auth/login ─────────────────────────────────────────────────
    // Web accounts live in web_accounts table — no HWID, no license required.
    if (method == "POST" && path == "/api/auth/login")
    {
        std::string username     = ParseJsonStr(body, "username");
        std::string passwordHash = ParseJsonStr(body, "passwordHash");
        if (username.empty() || passwordHash.empty())
        { SendApiError(s, 400, "username and passwordHash required"); return; }

        if (username.size() < 3 || username.size() > 32)
        { SendApiError(s, 400, "username must be 3-32 characters"); return; }

        std::optional<WebAccountRow> acct;
        {
            std::lock_guard lk(g_dbMu);
            acct = g_db.GetWebAccount(username);
        }
        if (!acct || acct->password_hash != passwordHash)
        {
            // Constant-time: always run comparison even when account not found
            SendApiError(s, 401, "Invalid username or password");
            return;
        }

        // Block login if email is set but not yet verified (newly registered accounts)
        if (!acct->email.empty() && acct->email_verified == 0)
        {
            SendApiError(s, 403, "Email not verified. Check your inbox for the verification code.");
            return;
        }

        std::string role = acct->role.empty() ? "user" : acct->role;
        { std::lock_guard lk(g_dbMu); g_db.UpdateWebAccountLastLogin(username, (int64_t)time(nullptr)); }

        WebSessionRow ws{};
        ws.token      = MakeWebToken();
        ws.username   = username;
        ws.role       = role;
        ws.created_at = (int64_t)time(nullptr);
        ws.expires_at = ws.created_at + 86400; // 24 h

        { std::lock_guard lk(g_dbMu); g_db.InsertWebSession(ws); }

        std::string resp = "{" + JStr("token", ws.token) + ","
                               + JStr("username", username) + ","
                               + JStr("role", role) + ","
                               + JStr("expiresAt", std::to_string(ws.expires_at)) + "}";
        SendJson(s, 200, resp);
        return;
    }

    // ── POST /api/auth/signup ─────────────────────────────────────────────────
    // Creates a new web_accounts row.  Requires email for OTP verification.
    if (method == "POST" && path == "/api/auth/signup")
    {
        std::string username     = ParseJsonStr(body, "username");
        std::string passwordHash = ParseJsonStr(body, "passwordHash");
        std::string email        = ParseJsonStr(body, "email");
        if (username.empty() || passwordHash.empty() || email.empty())
        { SendApiError(s, 400, "username, passwordHash, and email required"); return; }

        if (username.size() < 3 || username.size() > 32)
        { SendApiError(s, 400, "username must be 3-32 characters"); return; }
        if (passwordHash.size() != 64)
        { SendApiError(s, 400, "passwordHash must be a 64-char SHA-256 hex string"); return; }
        // Basic email sanity: must contain @ and a dot after it
        {
            size_t at = email.find('@');
            if (at == std::string::npos || email.find('.', at) == std::string::npos || email.size() > 254)
            { SendApiError(s, 400, "Invalid email address"); return; }
        }

        for (char c : username) {
            if (!std::isalnum((unsigned char)c) && c != '_')
            { SendApiError(s, 400, "username may only contain letters, digits, and underscores"); return; }
        }

        // Check email uniqueness
        {
            std::lock_guard lk(g_dbMu);
            if (g_db.GetWebAccountByEmail(email).has_value())
            { SendApiError(s, 409, "Email already registered"); return; }
        }

        // Determine role
        std::string role = "user";
        {
            const char* ownerEnv = std::getenv("SPARKY_WEB_OWNER_USERNAME");
            if (ownerEnv && std::string(ownerEnv) == username)
                role = "owner";
        }

        WebAccountRow acct{};
        acct.username       = username;
        acct.password_hash  = passwordHash;
        acct.role           = role;
        acct.created_at     = (int64_t)time(nullptr);
        acct.last_login     = acct.created_at;
        acct.email          = email;
        acct.email_verified = 0;

        bool created = false;
        { std::lock_guard lk(g_dbMu); created = g_db.CreateWebAccount(acct); }
        if (!created)
        { SendApiError(s, 409, "Username already taken"); return; }

        // Generate and store OTP (valid 10 minutes)
        std::string otp     = MakeOtp();
        int64_t     otpExp  = (int64_t)time(nullptr) + 600;
        { std::lock_guard lk(g_dbMu); g_db.SetWebAccountOtp(username, otp, otpExp); }

        // Send verification email (fire-and-forget — don't fail signup if email send fails)
        std::string emailHtml =
            "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\"></head>"
            "<body style=\"margin:0;padding:0;background:#0a0a0c;font-family:Arial,sans-serif;\">"
            "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" style=\"background:#0a0a0c;\">"
            "<tr><td align=\"center\" style=\"padding:48px 24px;\">"
            "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" style=\"max-width:480px;\">"
            "<tr><td style=\"padding-bottom:32px;text-align:center;\">"
            "<span style=\"font-size:22px;font-weight:700;color:#f0f0f5;letter-spacing:-0.5px;\">Sparky</span>"
            "</td></tr>"
            "<tr><td style=\"background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.08);"
            "border-radius:12px;padding:40px 36px;\">"
            "<h2 style=\"margin:0 0 8px;font-size:20px;font-weight:600;color:#f0f0f5;\">Verify your account</h2>"
            "<p style=\"margin:0 0 28px;font-size:14px;color:#888888;line-height:1.6;\">"
            "Enter the code below to verify your email and complete your registration.</p>"
            "<div style=\"background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.1);"
            "border-radius:8px;padding:24px;text-align:center;margin-bottom:28px;\">"
            "<span style=\"font-size:36px;font-weight:700;color:#f0f0f5;letter-spacing:12px;"
            "font-family:monospace;\">" + otp + "</span>"
            "</div>"
            "<p style=\"margin:0;font-size:13px;color:#888888;\">This code expires in "
            "<span style=\"color:#f0f0f5;\">10 minutes</span>. Do not share it with anyone.</p>"
            "</td></tr>"
            "<tr><td style=\"padding-top:24px;text-align:center;\">"
            "<p style=\"margin:0;font-size:12px;color:rgba(255,255,255,0.3);\">"
            "If you did not create a Sparky account, you can safely ignore this email.</p>"
            "</td></tr>"
            "</table></td></tr></table>"
            "</body></html>";
        SendResendEmail(email, "Verify your Sparky account", emailHtml);

        // Return 202 — pending OTP verification (no session token yet)
        std::string resp = "{" + JStr("status", "pending") + ","
                               + JStr("username", username) + "}";
        SendJson(s, 202, resp);
        return;
    }

    // ── POST /api/auth/verify-otp ─────────────────────────────────────────────
    // Verifies the signup OTP and issues a session token.
    if (method == "POST" && path == "/api/auth/verify-otp")
    {
        std::string username = ParseJsonStr(body, "username");
        std::string otp      = ParseJsonStr(body, "otp");
        if (username.empty() || otp.empty())
        { SendApiError(s, 400, "username and otp required"); return; }

        std::optional<WebAccountRow> acct;
        { std::lock_guard lk(g_dbMu); acct = g_db.GetWebAccount(username); }
        if (!acct)
        { SendApiError(s, 404, "Account not found"); return; }

        if (acct->email_verified)
        { SendApiError(s, 400, "Email already verified"); return; }

        int64_t now = (int64_t)time(nullptr);

        if (acct->otp_fail_count >= 5)
        { SendApiError(s, 429, "Too many failed attempts. Request a new verification code."); return; }

        if (acct->otp_code != otp || acct->otp_expires < now)
        {
            { std::lock_guard lk(g_dbMu); g_db.IncrementOtpFailCount(username); }
            SendApiError(s, 400, "Invalid or expired verification code"); return;
        }

        { std::lock_guard lk(g_dbMu); g_db.VerifyWebAccountEmail(username); }

        std::string role = acct->role.empty() ? "user" : acct->role;
        { std::lock_guard lk(g_dbMu); g_db.UpdateWebAccountLastLogin(username, now); }

        WebSessionRow ws{};
        ws.token      = MakeWebToken();
        ws.username   = username;
        ws.role       = role;
        ws.created_at = now;
        ws.expires_at = now + 86400;
        { std::lock_guard lk(g_dbMu); g_db.InsertWebSession(ws); }

        std::string resp = "{" + JStr("token", ws.token) + ","
                               + JStr("username", username) + ","
                               + JStr("role", role) + ","
                               + JStr("expiresAt", std::to_string(ws.expires_at)) + "}";
        SendJson(s, 200, resp);
        return;
    }

    // ── POST /api/auth/forgot-password ────────────────────────────────────────
    // Sends a password-reset OTP to the registered email address.
    // Always returns 200 to avoid revealing whether an account exists.
    if (method == "POST" && path == "/api/auth/forgot-password")
    {
        std::string email = ParseJsonStr(body, "email");
        if (email.empty())
        { SendApiError(s, 400, "email required"); return; }

        std::optional<WebAccountRow> acct;
        { std::lock_guard lk(g_dbMu); acct = g_db.GetWebAccountByEmail(email); }

        if (acct) {
            std::string otp    = MakeOtp();
            int64_t     otpExp = (int64_t)time(nullptr) + 600;
            { std::lock_guard lk(g_dbMu); g_db.SetWebAccountOtp(acct->username, otp, otpExp); }

            std::string emailHtml =
                "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
                "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\"></head>"
                "<body style=\"margin:0;padding:0;background:#0a0a0c;font-family:Arial,sans-serif;\">"
                "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" style=\"background:#0a0a0c;\">"
                "<tr><td align=\"center\" style=\"padding:48px 24px;\">"
                "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" style=\"max-width:480px;\">"
                "<tr><td style=\"padding-bottom:32px;text-align:center;\">"
                "<span style=\"font-size:22px;font-weight:700;color:#f0f0f5;letter-spacing:-0.5px;\">Sparky</span>"
                "</td></tr>"
                "<tr><td style=\"background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.08);"
                "border-radius:12px;padding:40px 36px;\">"
                "<h2 style=\"margin:0 0 8px;font-size:20px;font-weight:600;color:#f0f0f5;\">Reset your password</h2>"
                "<p style=\"margin:0 0 28px;font-size:14px;color:#888888;line-height:1.6;\">"
                "Enter the code below along with your username to set a new password.</p>"
                "<div style=\"background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.1);"
                "border-radius:8px;padding:24px;text-align:center;margin-bottom:28px;\">"
                "<span style=\"font-size:36px;font-weight:700;color:#f0f0f5;letter-spacing:12px;"
                "font-family:monospace;\">" + otp + "</span>"
                "</div>"
                "<p style=\"margin:0;font-size:13px;color:#888888;\">This code expires in "
                "<span style=\"color:#f0f0f5;\">10 minutes</span>. Do not share it with anyone.</p>"
                "</td></tr>"
                "<tr><td style=\"padding-top:24px;text-align:center;\">"
                "<p style=\"margin:0;font-size:12px;color:rgba(255,255,255,0.3);\">"
                "If you did not request a password reset, you can safely ignore this email.</p>"
                "</td></tr>"
                "</table></td></tr></table>"
                "</body></html>";
            SendResendEmail(email, "Reset your Sparky password", emailHtml);
        }

        SendJson(s, 200, "{\"status\":\"sent\"}");
        return;
    }

    // ── POST /api/auth/reset-password ─────────────────────────────────────────
    // Verifies the reset OTP and updates the password.
    if (method == "POST" && path == "/api/auth/reset-password")
    {
        std::string username     = ParseJsonStr(body, "username");
        std::string otp          = ParseJsonStr(body, "otp");
        std::string newPwdHash   = ParseJsonStr(body, "newPasswordHash");
        if (username.empty() || otp.empty() || newPwdHash.empty())
        { SendApiError(s, 400, "username, otp, and newPasswordHash required"); return; }
        if (newPwdHash.size() != 64)
        { SendApiError(s, 400, "newPasswordHash must be a 64-char SHA-256 hex string"); return; }

        std::optional<WebAccountRow> acct;
        { std::lock_guard lk(g_dbMu); acct = g_db.GetWebAccount(username); }
        if (!acct)
        { SendApiError(s, 404, "Account not found"); return; }

        int64_t now = (int64_t)time(nullptr);

        if (acct->otp_fail_count >= 5)
        { SendApiError(s, 429, "Too many failed attempts. Request a new reset code."); return; }

        if (acct->otp_code != otp || acct->otp_expires < now)
        {
            { std::lock_guard lk(g_dbMu); g_db.IncrementOtpFailCount(username); }
            SendApiError(s, 400, "Invalid or expired reset code"); return;
        }

        { std::lock_guard lk(g_dbMu); g_db.UpdateWebAccountPassword(username, newPwdHash); }

        SendJson(s, 200, "{\"status\":\"ok\"}");
        return;
    }

    // ── POST /api/auth/logout ─────────────────────────────────────────────────
    if (method == "POST" && path == "/api/auth/logout")
    {
        std::string token = ExtractBearerToken(req);
        if (!token.empty()) {
            std::lock_guard lk(g_dbMu);
            g_db.DeleteWebSession(token);
        }
        SendJson(s, 200, "{}");
        return;
    }

    // ── GET /api/auth/me ──────────────────────────────────────────────────────
    if (method == "GET" && path == "/api/auth/me")
    {
        std::string token = ExtractBearerToken(req);
        WebAuthInfo auth  = ValidateWebToken(token);
        if (!auth.ok) { SendApiError(s, 401, "Not authenticated"); return; }

        // Re-read the current role from web_accounts (may have been updated)
        {
            std::lock_guard lk(g_dbMu);
            auto acct = g_db.GetWebAccount(auth.username);
            if (acct) auth.role = acct->role;
        }

        std::string resp = "{" + JStr("username", auth.username) + ","
                               + JStr("role", auth.role) + ","
                               + JStr("hwid", "") + ","
                               + JStr("licenseKey", "") + ","
                               + JStr("expiresAt", "0") + "}";
        SendJson(s, 200, resp);
        return;
    }

    // ── GET /api/download ────────────────────────────────────────────────────
    if (method == "GET" && path == "/api/download")
    {
        std::string token = ExtractBearerToken(req);
        WebAuthInfo auth  = ValidateWebToken(token);
        if (!auth.ok) { SendApiError(s, 401, "Not authenticated"); return; }

        std::vector<uint8_t> loaderBytes = ReadFileFull("/app/payloads/SparkyLoader.exe");
        if (loaderBytes.empty()) loaderBytes = ReadFileFull("SparkyLoader.exe");
        if (loaderBytes.empty()) { SendApiError(s, 404, "Loader not available"); return; }

        std::string name = GenerateRandomString(8) + ".exe";
        std::string hdr  =
            "HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Disposition: attachment; filename=\"" + name + "\"\r\n"
            "Content-Type: application/x-msdownload\r\n"
            "Content-Length: " + std::to_string(loaderBytes.size()) + "\r\n\r\n";
        NetSend(s.sock, s.ssl, hdr.c_str(), (int)hdr.size());
        NetSend(s.sock, s.ssl, loaderBytes.data(), (int)loaderBytes.size());
        return;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Everything below requires at least admin role.
    // ─────────────────────────────────────────────────────────────────────────

    // ── GET /api/admin/users ──────────────────────────────────────────────────
    if (method == "GET" && path == "/api/admin/users")
    {
        std::string token = ExtractBearerToken(req);
        WebAuthInfo auth  = ValidateWebToken(token);
        if (!auth.ok) { SendApiError(s, 401, "Not authenticated"); return; }
        if (auth.role != "admin" && auth.role != "owner")
        { SendApiError(s, 403, "Admin access required"); return; }

        std::vector<UserRow> users;
        { std::lock_guard lk(g_dbMu); users = g_db.ListUsers(); }

        std::string arr = "[";
        for (size_t i = 0; i < users.size(); ++i)
        {
            auto& u = users[i];
            if (i) arr += ",";
            arr += "{" + JStr("id", u.hwid_hash) + ","
                       + JStr("username", u.username) + ","
                       + JStr("hwid", u.hwid_hash) + ","
                       + JStr("licenseKey", u.license_key) + ","
                       + JBool("isBanned", u.is_banned) + ","
                       + JStr("lastSeen", std::to_string(u.last_seen)) + "}";
        }
        arr += "]";
        SendJson(s, 200, arr);
        return;
    }

    // ── POST /api/admin/users/ban ──────────────────────────────────────────────
    if (method == "POST" && path == "/api/admin/users/ban")
    {
        std::string token = ExtractBearerToken(req);
        WebAuthInfo auth  = ValidateWebToken(token);
        if (!auth.ok) { SendApiError(s, 401, "Not authenticated"); return; }
        if (auth.role != "admin" && auth.role != "owner")
        { SendApiError(s, 403, "Admin access required"); return; }

        std::string hwid = ParseJsonStr(body, "hwid");
        if (hwid.empty()) { SendApiError(s, 400, "hwid required"); return; }
        {
            std::lock_guard lk(g_dbMu);
            g_db.BanUser(hwid, "web admin ban");
            g_db.DeleteSessionsByHwid(hwid);
        }
        {
            std::lock_guard lk(g_hwidMu);
            g_activeHwids.erase(hwid);
        }
        SendJson(s, 200, "{}");
        return;
    }

    // ── POST /api/admin/users/unban ────────────────────────────────────────────
    if (method == "POST" && path == "/api/admin/users/unban")
    {
        std::string token = ExtractBearerToken(req);
        WebAuthInfo auth  = ValidateWebToken(token);
        if (!auth.ok) { SendApiError(s, 401, "Not authenticated"); return; }
        if (auth.role != "admin" && auth.role != "owner")
        { SendApiError(s, 403, "Admin access required"); return; }

        std::string hwid = ParseJsonStr(body, "hwid");
        if (hwid.empty()) { SendApiError(s, 400, "hwid required"); return; }
        { std::lock_guard lk(g_dbMu); g_db.UnbanUser(hwid); }
        SendJson(s, 200, "{}");
        return;
    }

    // ── GET /api/admin/licenses ────────────────────────────────────────────────
    if (method == "GET" && path == "/api/admin/licenses")
    {
        std::string token = ExtractBearerToken(req);
        WebAuthInfo auth  = ValidateWebToken(token);
        if (!auth.ok) { SendApiError(s, 401, "Not authenticated"); return; }
        if (auth.role != "admin" && auth.role != "owner")
        { SendApiError(s, 403, "Admin access required"); return; }

        std::vector<LicenseRow> licenses;
        { std::lock_guard lk(g_dbMu); licenses = g_db.ListLicenses(); }

        std::string arr = "[";
        for (size_t i = 0; i < licenses.size(); ++i)
        {
            auto& l = licenses[i];
            if (i) arr += ",";
            arr += "{" + JStr("key", l.key) + ","
                       + JNum("tier", l.tier) + ","
                       + JStr("hwid", l.hwid_hash) + ","
                       + JStr("expiresAt", std::to_string(l.expires_at)) + ","
                       + JBool("isBound", !l.hwid_hash.empty()) + "}";
        }
        arr += "]";
        SendJson(s, 200, arr);
        return;
    }

    // ── POST /api/admin/licenses/issue ────────────────────────────────────────
    if (method == "POST" && path == "/api/admin/licenses/issue")
    {
        std::string token = ExtractBearerToken(req);
        WebAuthInfo auth  = ValidateWebToken(token);
        if (!auth.ok) { SendApiError(s, 401, "Not authenticated"); return; }
        if (auth.role != "admin" && auth.role != "owner")
        { SendApiError(s, 403, "Admin access required"); return; }

        int tier = ParseJsonInt(body, "tier");
        int days = ParseJsonInt(body, "days");
        if (tier < 1 || tier > 4) { SendApiError(s, 400, "tier must be 1-4"); return; }
        if (days < 0) { SendApiError(s, 400, "days must be non-negative"); return; }

        std::string key;
        { std::lock_guard lk(g_dbMu); key = g_lm->IssueLicense(tier, days); }
        if (key.empty()) { SendApiError(s, 500, "Failed to issue license"); return; }

        SendJson(s, 200, "{" + JStr("key", key) + "}");
        return;
    }

    // ── GET /api/admin/sessions ────────────────────────────────────────────────
    if (method == "GET" && path == "/api/admin/sessions")
    {
        std::string token = ExtractBearerToken(req);
        WebAuthInfo auth  = ValidateWebToken(token);
        if (!auth.ok) { SendApiError(s, 401, "Not authenticated"); return; }
        if (auth.role != "admin" && auth.role != "owner")
        { SendApiError(s, 403, "Admin access required"); return; }

        SendJson(s, 200, "{" + JNum("count", g_activeSessions.load()) + "}");
        return;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Everything below requires owner role.
    // ─────────────────────────────────────────────────────────────────────────

    // ── GET /api/owner/admins ─────────────────────────────────────────────────
    // Lists all web_accounts with role='admin' or 'owner'
    if (method == "GET" && path == "/api/owner/admins")
    {
        std::string token = ExtractBearerToken(req);
        WebAuthInfo auth  = ValidateWebToken(token);
        if (!auth.ok) { SendApiError(s, 401, "Not authenticated"); return; }
        if (auth.role != "owner") { SendApiError(s, 403, "Owner access required"); return; }

        std::vector<WebAccountRow> admins;
        { std::lock_guard lk(g_dbMu); admins = g_db.ListWebAdmins(); }

        std::string arr = "[";
        for (size_t i = 0; i < admins.size(); ++i)
        {
            auto& a = admins[i];
            if (i) arr += ",";
            arr += "{" + JStr("username", a.username) + ","
                       + JStr("hwid", "") + ","
                       + JStr("role", a.role) + ","
                       + JStr("grantedAt", std::to_string(a.created_at)) + "}";
        }
        arr += "]";
        SendJson(s, 200, arr);
        return;
    }

    // ── POST /api/owner/admins/grant ──────────────────────────────────────────
    if (method == "POST" && path == "/api/owner/admins/grant")
    {
        std::string token = ExtractBearerToken(req);
        WebAuthInfo auth  = ValidateWebToken(token);
        if (!auth.ok) { SendApiError(s, 401, "Not authenticated"); return; }
        if (auth.role != "owner") { SendApiError(s, 403, "Owner access required"); return; }

        std::string username = ParseJsonStr(body, "username");
        if (username.empty()) { SendApiError(s, 400, "username required"); return; }
        { std::lock_guard lk(g_dbMu); g_db.SetWebAccountRole(username, "admin"); }
        SendJson(s, 200, "{}");
        return;
    }

    // ── POST /api/owner/admins/revoke ─────────────────────────────────────────
    if (method == "POST" && path == "/api/owner/admins/revoke")
    {
        std::string token = ExtractBearerToken(req);
        WebAuthInfo auth  = ValidateWebToken(token);
        if (!auth.ok) { SendApiError(s, 401, "Not authenticated"); return; }
        if (auth.role != "owner") { SendApiError(s, 403, "Owner access required"); return; }

        std::string username = ParseJsonStr(body, "username");
        if (username.empty()) { SendApiError(s, 400, "username required"); return; }
        { std::lock_guard lk(g_dbMu); g_db.SetWebAccountRole(username, "user"); }
        SendJson(s, 200, "{}");
        return;
    }

    // ── GET /api/owner/metrics ────────────────────────────────────────────────
    if (method == "GET" && path == "/api/owner/metrics")
    {
        std::string token = ExtractBearerToken(req);
        WebAuthInfo auth  = ValidateWebToken(token);
        if (!auth.ok) { SendApiError(s, 401, "Not authenticated"); return; }
        if (auth.role != "owner") { SendApiError(s, 403, "Owner access required"); return; }

        int64_t uptime = (int64_t)time(nullptr) - g_startTime;

        std::vector<UserRow>    allUsers;
        std::vector<LicenseRow> allLicenses;
        {
            std::lock_guard lk(g_dbMu);
            allUsers    = g_db.ListUsers();
            allLicenses = g_db.ListLicenses();
        }

        bool   dllLoaded = !g_dllBytes.empty();
        size_t dllSize   = g_dllBytes.size();

        // Build version string from CURRENT_BUILD constant
        char buildStr[16];
        snprintf(buildStr, sizeof(buildStr), "%d.%d.%d",
                 (int)((CURRENT_BUILD >> 16) & 0xFF),
                 (int)((CURRENT_BUILD >>  8) & 0xFF),
                 (int)( CURRENT_BUILD        & 0xFF));

        std::string resp = "{"
            + JNum("activeSessions",  g_activeSessions.load()) + ","
            + JNum("uptimeSeconds",   uptime) + ","
            + JBool("dllLoaded",      dllLoaded) + ","
            + JNum("dllSizeBytes",    (int64_t)dllSize) + ","
            + JNum("totalUsers",      (int64_t)allUsers.size()) + ","
            + JNum("totalLicenses",   (int64_t)allLicenses.size()) + ","
            + JStr("buildVersion",    buildStr) + ","
            + JNum("timestamp",       (int64_t)time(nullptr))
            + "}";
        SendJson(s, 200, resp);
        return;
    }

    // ── 404 fallthrough ───────────────────────────────────────────────────────
    SendApiError(s, 404, "Not found");
}

// ---------------------------------------------------------------------------
// StreamEncryptedDll — heartbeat-gated, rolling-key chunked DLL delivery.
// ---------------------------------------------------------------------------
static bool StreamEncryptedDll(ClientSession& s)
{
    if (g_dllBytes.empty()) return false;

    const uint32_t total      = (uint32_t)g_dllBytes.size();
    const uint32_t nChunks    = (total + CHUNK_SIZE - 1) / CHUNK_SIZE;
    const uint32_t hbInterval = CHUNKS_PER_HEARTBEAT;

    BinaryReadyPayload br{};
    br.TotalBytes         = total;
    br.ChunkSize          = CHUNK_SIZE;
    br.NumChunks          = nChunks;
    br.ChunksPerHeartbeat = hbInterval;
    if (!SendMsg(s, MsgType::BinaryReady, &br, sizeof(br))) return false;

    std::cout << std::format("[S] Streaming {:.16}... ({} chunks, key rolls every {})\n",
                              s.hwid, nChunks, hbInterval);

    uint64_t rollingKey = s.dllKey;

    for (uint32_t c = 0; c < nChunks; ++c)
    {
        const uint32_t off  = c * CHUNK_SIZE;
        const uint32_t size = (uint32_t)std::min((size_t)CHUNK_SIZE,
                                                   (size_t)(total - off));

        std::vector<uint8_t> chunk(g_dllBytes.begin() + off,
                                    g_dllBytes.begin() + off + size);
        XorStream(chunk.data(), size, rollingKey);

        if (!SendMsg(s, MsgType::BinaryChunk, chunk.data(), (uint16_t)size))
            return false;

        if ((c + 1) % hbInterval == 0 || c + 1 == nChunks)
        {
            MsgType mt{}; std::vector<uint8_t> mp;
            if (!RecvMsg(s, mt, mp, HEARTBEAT_DEADLINE_MS))
            {
                std::cout << std::format(
                    "[S] HB timeout chunk {}/{} for {:.16}... — drop\n",
                    c + 1, nChunks, s.hwid);
                return false;
            }
            if (mt != MsgType::Heartbeat)
            {
                std::cout << "[S] Expected Heartbeat — got wrong msg type, dropping\n";
                return false;
            }

            uint8_t nonce[16]{};
            if (mp.size() >= sizeof(HeartbeatPayload))
                memcpy(nonce, mp.data(), 16);

            rollingKey = RollKey(rollingKey, nonce);
            SendMsg(s, MsgType::Ack);
            {
                std::lock_guard lk(g_dbMu);
                g_db.TouchSession(s.tokenHex, (int64_t)time(nullptr));
            }
        }
    }

    return SendMsg(s, MsgType::BinaryEnd);
}

// ---------------------------------------------------------------------------
// HandleClient
// ---------------------------------------------------------------------------
#ifdef _WIN32
static void HandleClient(SOCKET csock, SSL* ssl, std::string clientIp)
#else
static void HandleClient(int csock, SSL* ssl, std::string clientIp)
#endif
{
    ClientSession s{};
    s.sock = csock;
    s.ssl  = ssl;
    s.ip   = clientIp;
    const int64_t now = (int64_t)time(nullptr);

    // Track whether this HWID has been registered in g_activeHwids.
    // Must be cleared on every exit path after registration.
    bool hwidRegistered = false;

    // Shared cleanup: HWID deregistration + TLS teardown + socket close.
    // Called at every early return and at the end of the function.
    auto cleanup = [&]() {
        if (hwidRegistered)
        {
            std::lock_guard lk(g_hwidMu);
            g_activeHwids.erase(s.hwid);
            hwidRegistered = false;
        }
        if (s.ssl) { SSL_shutdown(s.ssl); SSL_free(s.ssl); s.ssl = nullptr; }
#ifdef _WIN32
        closesocket(csock);
#else
        ::close(csock);
#endif
        // Decrement per-IP connection counter
        if (!clientIp.empty())
        {
            std::lock_guard lk(g_connMu);
            auto it = g_connCount.find(clientIp);
            if (it != g_connCount.end() && it->second > 0)
            {
                if (--(it->second) == 0) g_connCount.erase(it);
            }
        }
    };

    // ---- 1. Receive Hello (or HTTP Health Check) ----
    MsgHeader h{};
    std::vector<uint8_t> pay;
    uint32_t crc = 0;
    if (!NetRecv(s.sock, s.ssl, &h, sizeof(h), 10000))
    {
        cleanup(); return;
    }

    // Check if it's an HTTP request (Load Balancer Health Check, React site API, or curl)
    if (memcmp(&h.Magic, "GET ", 4) == 0 || memcmp(&h.Magic, "HEAD", 4) == 0
        || memcmp(&h.Magic, "POST", 4) == 0 || memcmp(&h.Magic, "OPTI", 4) == 0)
    {
        std::string req;
        req.append((const char*)&h, sizeof(h));
        char c;
        // Slowloris mitigation: impose a hard deadline on the entire header read.
        // Even with per-call NetRecv timeouts, an attacker trickle-sending one
        // byte every 4.9 s could hold the thread open for hours.  SO_RCVTIMEO
        // caps each underlying recv() call at the OS level.
        {
#ifdef _WIN32
            DWORD hdrTo = 5000;
            setsockopt(csock, SOL_SOCKET, SO_RCVTIMEO, (char*)&hdrTo, sizeof(hdrTo));
#else
            struct timeval hdrTo{5, 0};
            setsockopt(csock, SOL_SOCKET, SO_RCVTIMEO, &hdrTo, sizeof(hdrTo));
#endif
        }
        // Read until \r\n\r\n or max 8KB
        while (req.size() < 8192 && NetRecv(s.sock, s.ssl, &c, 1, 5000))
        {
            req += c;
            if (req.size() >= 4 && req.compare(req.size() - 4, 4, "\r\n\r\n") == 0)
                break;
        }

        // Extract HTTP method and path from the request line
        std::string httpMethod, httpPath;
        {
            size_t sp1 = req.find(' ');
            if (sp1 != std::string::npos)
            {
                httpMethod = req.substr(0, sp1);
                size_t sp2 = req.find(' ', sp1 + 1);
                if (sp2 != std::string::npos)
                    httpPath = req.substr(sp1 + 1, sp2 - sp1 - 1);
            }
        }

        // Answer CORS preflight before the SPARKY_KEY check — the browser
        // doesn't send custom headers in OPTIONS, so auth is not possible here.
        if (httpMethod == "OPTIONS")
        {
            const std::string& corsOrigin = g_allowedOrigin.empty()
                ? std::string("*") : g_allowedOrigin;
            std::string cors =
                std::string("HTTP/1.1 204 No Content\r\n") +
                "Access-Control-Allow-Origin: " + corsOrigin + "\r\n" +
                "Vary: Origin\r\n"
                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                "Access-Control-Max-Age: 86400\r\n"
                "Content-Length: 0\r\n\r\n";
            NetSend(s.sock, s.ssl, cors.c_str(), (int)cors.size());
            cleanup(); return;
        }

        // /api/* routes authenticate via Bearer token inside HandleWebApi —
        // they never need the loader SPARKY_KEY, so we skip that check entirely.
        // This means the key is never transmitted from the browser.
        const bool isWebApiPath = (httpPath.size() >= 5 &&
                                   httpPath.compare(0, 5, "/api/") == 0);

        bool authorized = g_sparkyKey.empty() || isWebApiPath;
        std::string _sk; // decrypted only once per request if needed
        std::string authHex;

        if (true) // always parse headers if it's HTTP
        {
            std::string lowerReq = req;
            for (auto& ch : lowerReq) ch = (char)std::tolower((unsigned char)ch);

            // 1. Check Cloud Armor / Secret Key (loader only — skipped for /api/*)
            if (!g_sparkyKey.empty() && !isWebApiPath)
            {
                size_t pos = lowerReq.find(XS("x-sparky-key:"));
                if (pos != std::string::npos)
                {
                    size_t start = pos + 13;
                    while (start < req.size() && (req[start] == ' ' || req[start] == '\t')) start++;
                    size_t end = start;
                    while (end < req.size() && req[end] != '\r' && req[end] != '\n') end++;

                    std::string submittedKey = req.substr(start, end - start);
                    _sk = g_sparkyKey.Get();
                    bool keyOk = submittedKey.size() == _sk.size() &&
                                 CRYPTO_memcmp(submittedKey.data(), _sk.data(), _sk.size()) == 0;
                    OPENSSL_cleanse(_sk.data(), _sk.size());
                    _sk.clear();
                    if (keyOk) authorized = true;
                }
            }

            // 2. Extract HelloPayload if this is a Loader Auth request
            size_t authPos = lowerReq.find(XS("x-sparky-auth:"));
            if (authPos != std::string::npos)
            {
                size_t start = authPos + 14;
                while (start < req.size() && (req[start] == ' ' || req[start] == '\t')) start++;
                size_t end = start;
                while (end < req.size() && req[end] != '\r' && req[end] != '\n') end++;
                authHex = req.substr(start, end - start);
            }

            // 3. Detect Connection: keep-alive
            {
                size_t cp = lowerReq.find("connection:");
                if (cp != std::string::npos)
                {
                    size_t vs = cp + 11;
                    while (vs < req.size() && (req[vs] == ' ' || req[vs] == '\t')) ++vs;
                    s.keepAlive = (lowerReq.compare(vs, 10, "keep-alive") == 0);
                }
            }

            // 4. Extract Sec-WebSocket-Key for WebSocket upgrade detection
            // (compare lowercase header name, but extract value from original req)
            size_t wsKeyPos = lowerReq.find(XS("sec-websocket-key:"));
            if (wsKeyPos != std::string::npos)
            {
                size_t start = wsKeyPos + 18; // len("sec-websocket-key:")
                while (start < req.size() && (req[start] == ' ' || req[start] == '\t')) start++;
                size_t end = start;
                while (end < req.size() && req[end] != '\r' && req[end] != '\n') end++;
                s.wsMode = true; // Upgrade: websocket present; capture key for 101 response
                // Temporarily store the key in tokenHex for the handshake
                s.tokenHex = req.substr(start, end - start);
            }
        }

        if (authorized)
        {
            // Read POST body based on Content-Length header
            std::string httpBody;
            if (httpMethod == "POST")
            {
                std::string lr2 = req;
                for (auto& ch : lr2) ch = (char)std::tolower((unsigned char)ch);
                size_t cp = lr2.find("content-length:");
                if (cp != std::string::npos)
                {
                    size_t vs = cp + 15;
                    while (vs < req.size() && (req[vs] == ' ' || req[vs] == '\t')) ++vs;
                    size_t ve = req.find('\r', vs);
                    if (ve == std::string::npos) ve = req.size();
                    try {
                        int cl = std::stoi(req.substr(vs, ve - vs));
                        if (cl > 64 * 1024) {
                            // Reject oversized bodies immediately — all API payloads
                            // are tiny JSON; anything larger is an attack or misclient.
                            SendApiError(s, 413, "Request body too large");
                            cleanup(); return;
                        }
                        if (cl > 0) {
                            httpBody.resize((size_t)cl);
                            NetRecv(s.sock, s.ssl, httpBody.data(), cl, 5000);
                        }
                    } catch (...) {}
                }
            }

            // Route /api/* to the web API handler.
            // Keep-alive loop: login → getMe → download can reuse the same TCP
            // connection, saving 2 extra TLS handshakes for the typical flow.
            if (httpPath.size() >= 5 && httpPath.compare(0, 5, "/api/") == 0)
            {
                while (true)
                {
                    HandleWebApi(s, httpMethod, httpPath, req, httpBody);
                    if (!s.keepAlive) break;

                    // Read next HTTP request on the same connection.
                    req.clear(); httpMethod.clear(); httpPath.clear(); httpBody.clear();
                    s.keepAlive = false; // re-set when parsing next request headers

                    // Read first 16 bytes (same framing as the initial MsgHeader read)
                    MsgHeader hn{};
                    if (!NetRecv(s.sock, s.ssl, &hn, sizeof(hn), 30000)) break;
                    if (memcmp(&hn.Magic, "GET ", 4) != 0 &&
                        memcmp(&hn.Magic, "POST", 4) != 0 &&
                        memcmp(&hn.Magic, "HEAD", 4) != 0 &&
                        memcmp(&hn.Magic, "OPTI", 4) != 0) break;
                    req.append((const char*)&hn, sizeof(hn));
                    while (req.size() < 8192 && NetRecv(s.sock, s.ssl, &c, 1, 5000))
                    {
                        req += c;
                        if (req.size() >= 4 && req.compare(req.size() - 4, 4, "\r\n\r\n") == 0)
                            break;
                    }
                    // Re-parse method and path
                    {
                        size_t sp1 = req.find(' ');
                        if (sp1 != std::string::npos)
                        {
                            httpMethod = req.substr(0, sp1);
                            size_t sp2 = req.find(' ', sp1 + 1);
                            if (sp2 != std::string::npos)
                                httpPath = req.substr(sp1 + 1, sp2 - sp1 - 1);
                        }
                    }
                    if (httpPath.size() < 5 || httpPath.compare(0, 5, "/api/") != 0) break;
                    // Re-detect keep-alive for this request
                    {
                        std::string lr = req;
                        for (auto& ch : lr) ch = (char)std::tolower((unsigned char)ch);
                        size_t cp = lr.find("connection:");
                        if (cp != std::string::npos)
                        {
                            size_t vs = cp + 11;
                            while (vs < req.size() && (req[vs] == ' ' || req[vs] == '\t')) ++vs;
                            s.keepAlive = (lr.compare(vs, 10, "keep-alive") == 0);
                        }
                    }
                    // Re-read body for POST
                    if (httpMethod == "POST")
                    {
                        std::string lr2 = req;
                        for (auto& ch : lr2) ch = (char)std::tolower((unsigned char)ch);
                        size_t cp = lr2.find("content-length:");
                        if (cp != std::string::npos)
                        {
                            size_t vs = cp + 15;
                            while (vs < req.size() && (req[vs] == ' ' || req[vs] == '\t')) ++vs;
                            size_t ve = req.find('\r', vs);
                            if (ve == std::string::npos) ve = req.size();
                            try {
                                int cl = std::stoi(req.substr(vs, ve - vs));
                                if (cl > 64 * 1024) { SendApiError(s, 413, "Request body too large"); break; }
                                if (cl > 0) { httpBody.resize((size_t)cl); NetRecv(s.sock, s.ssl, httpBody.data(), cl, 5000); }
                            } catch (...) {}
                        }
                    }
                }
                cleanup(); return;
            }

            // If the loader sent an auth header, we treat this as the 'Hello' phase.
            if (!authHex.empty())
            {
                HelloPayload hp{};
                if (ParseHex(authHex, reinterpret_cast<uint8_t*>(&hp), sizeof(hp)))
                {
                    if (s.wsMode && !s.tokenHex.empty())
                    {
                        // WebSocket origin validation — block WS upgrades from unknown
                        // web origins.  Loader connections have no Origin header and are
                        // always allowed through; only browser-initiated WS is checked.
                        if (!g_allowedOrigin.empty())
                        {
                            std::string lrws = req;
                            for (auto& ch : lrws) ch = (char)std::tolower((unsigned char)ch);
                            size_t op = lrws.find("origin:");
                            if (op != std::string::npos)
                            {
                                size_t vs = op + 7;
                                while (vs < req.size() && (req[vs]==' '||req[vs]=='\t')) ++vs;
                                size_t ve = req.find('\r', vs);
                                if (ve == std::string::npos) ve = req.size();
                                std::string origin = req.substr(vs, ve - vs);
                                if (origin != g_allowedOrigin)
                                {
                                    const char* rej = "HTTP/1.1 403 Forbidden\r\nContent-Length: 9\r\n\r\nForbidden";
                                    NetSend(s.sock, s.ssl, rej, (int)strlen(rej));
                                    cleanup(); return;
                                }
                            }
                        }

                        // WebSocket upgrade path — sends RFC 6455 §4.2.2 handshake.
                        // The GCP HTTP LB then proxies frames transparently, allowing
                        // binary protocol messages to flow in both directions.
                        std::string accept = WsComputeAccept(s.tokenHex);
                        std::string resp =
                            std::string(XS("HTTP/1.1 101 Switching Protocols\r\n")) +
                            XS("Upgrade: websocket\r\n") +
                            XS("Connection: Upgrade\r\n") +
                            XS("Sec-WebSocket-Accept: ") + accept + XS("\r\n\r\n");
                        NetSend(s.sock, s.ssl, resp.c_str(), (int)resp.size());
                        s.tokenHex.clear(); // will be overwritten later with the real token hex
                        std::cout << "[S] WebSocket upgrade complete — switching to WS framing\n";
                    }
                    else
                    {
                        // Legacy plain HTTP path (kept for backward compatibility)
                        s.wsMode = false;
                        std::string ok = XS("HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n");
                        NetSend(s.sock, s.ssl, ok.c_str(), (int)ok.size());
                        std::cout << "[S] HTTP Auth detected (legacy — no WS upgrade)\n";
                    }

                    std::vector<uint8_t> fakePay(reinterpret_cast<uint8_t*>(&hp),
                                                  reinterpret_cast<uint8_t*>(&hp) + sizeof(hp));
                    pay = std::move(fakePay);
                    goto AUTH_LOGIC_START;
                }
            }
            if (req.find(XS("GET /download")) != std::string::npos || req.find(XS("GET /loader")) != std::string::npos)
            {
                // ... (existing loader download logic)
                std::vector<uint8_t> loaderBytes = ReadFileFull(XS("/app/payloads/SparkyLoader.exe"));
                if (loaderBytes.empty()) loaderBytes = ReadFileFull(XS("SparkyLoader.exe")); 

                if (!loaderBytes.empty())
                {
                    std::random_device rd; std::mt19937 gen(rd());
                    std::uniform_int_distribution<> distLen(16, 64);
                    std::uniform_int_distribution<uint8_t> distByte(0, 255);
                    int appendLen = distLen(gen);
                    for (int i = 0; i < appendLen; ++i) loaderBytes.push_back(distByte(gen));

                    std::string random_name = GenerateRandomString(8) + XS(".exe");
                    std::string headers =
                        std::string(XS("HTTP/1.1 200 OK\r\n")) +
                        XS("Content-Disposition: attachment; filename=\"") + random_name + XS("\"\r\n") +
                        XS("X-Sparky-Payload-Name: ") + random_name + XS("\r\n") +
                        XS("Content-Type: application/x-msdownload\r\n") +
                        XS("Content-Length: ") + std::to_string(loaderBytes.size()) + XS("\r\n\r\n");

                    NetSend(s.sock, s.ssl, headers.c_str(), (int)headers.size());
                    NetSend(s.sock, s.ssl, loaderBytes.data(), (int)loaderBytes.size());
                    std::cout << "[S] Served Polymorphic Loader (" << random_name << ", " << loaderBytes.size() << " bytes)\n";
                }
                else
                {
                }
            }
            else
            {
                const char* resp = XS("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
                NetSend(s.sock, s.ssl, resp, (int)strlen(resp));
                std::cout << "[S] HTTP Health check / Auth OK\n";
            }
        }
        else
        {
            const char* resp = XS("HTTP/1.1 403 Forbidden\r\nContent-Length: 9\r\n\r\nForbidden");
            NetSend(s.sock, s.ssl, resp, (int)strlen(resp));
            std::cout << "[S] HTTP Health check / Auth Failed (missing or bad x-sparky-key)\n";
        }

        cleanup();
        return;
    }

    if (h.Magic   != PROTO_MAGIC
        || h.Version != PROTO_VERSION
        || h.Type    != MsgType::Hello)
    {
        cleanup(); return;
    }
    // HelloPayload is fixed-size: reject anything shorter or suspiciously large
    if (h.Length < sizeof(HelloPayload) || h.Length > sizeof(HelloPayload) + 256)
    {
        cleanup(); return;
    }
    pay.resize(h.Length);
    if (!NetRecv(s.sock, s.ssl, pay.data(), h.Length, 10000)) { cleanup(); return; }
    if (!NetRecv(s.sock, s.ssl, &crc, 4, 10000)) { cleanup(); return; }

    // Verify Hello CRC
    {
        uint32_t computed = Crc32((uint8_t*)&h, sizeof(h));
        if (!pay.empty()) computed ^= Crc32(pay.data(), (uint32_t)pay.size());
        if (computed != crc) { cleanup(); return; }
    }

    // Padding must be zero — non-zero bytes indicate a malformed or fuzzed frame
    if (h.Pad[0] != 0 || h.Pad[1] != 0 || h.Pad[2] != 0 || h.Pad[3] != 0)
    {
        std::cout << "[S] Reject: non-zero MsgHeader padding — dropping connection\n";
        cleanup(); return;
    }

    if (pay.size() < sizeof(HelloPayload)) { cleanup(); return; }
AUTH_LOGIC_START:
    const auto& hello = *reinterpret_cast<HelloPayload*>(pay.data());

    // Raw HWID hex from the loader
    s.hwid       = HexStr(hello.HwidHash,   32);
    // ---- 1b. Loader attestation ----
    // When SPARKY_ATTEST_KEY is set the loader sends HMAC-SHA256(raw_binary_hash, attest_key)
    // rather than the raw hash.  We verify by computing the expected HMAC for every trusted
    // hash and comparing — the raw hash is never transmitted, only the keyed HMAC.
    if (g_attestKeySet)
    {
        bool attested = false;
        std::vector<TrustedHashRow> hashes;
        { std::lock_guard lk(g_dbMu); hashes = g_db.ListHashes(); }

        for (const auto& th : hashes)
        {
            uint8_t rawBytes[32]{};
            if (!ParseHex(th.hash, rawBytes, 32)) continue;

            uint8_t expected[32]{};
            unsigned elen = 32;
            HMAC(EVP_sha256(), g_attestKey, 32, rawBytes, 32, expected, &elen);

            if (memcmp(expected, hello.LoaderHash, 32) == 0)
            {
                s.loaderHash = th.hash; // store the canonical raw hash in DB
                attested     = true;
                break;
            }
        }

        if (!attested)
        {
            std::cout << "[S] Reject: loader attestation failed (unknown or modified binary)\n";
            SendMsg(s, MsgType::AuthFail);
            cleanup(); return;
        }
    }
    else
    {
        // Attest key not configured — accept raw hash (dev/legacy mode)
        s.loaderHash = HexStr(hello.LoaderHash, 32);
    }

    // Apply server-side pepper before any DB lookup or storage
    if (g_pepperSet) s.hwid = PepperHwid(s.hwid);

    std::cout << std::format("[S] Hello HWID={:.16}... build={:08X} loader={:.16}...\n",
                              s.hwid, hello.BuildId, s.loaderHash);

    // ---- 2. Build ID check & Anti-Replay ----
    if (hello.BuildId != CURRENT_BUILD)
    {
        std::cout << "[S] Reject: stale build\n";
        SendMsg(s, MsgType::AuthFail);
        cleanup(); return;
    }

    long long timeDiff = (long long)now - (long long)hello.Timestamp;
    if (timeDiff < -30 || timeDiff > 30)
    {
        std::cout << std::format("[S] Reject: Replay Attack protection (timestamp {} vs server {})\n",
                                  hello.Timestamp, now);
        SendMsg(s, MsgType::AuthFail);
        cleanup(); return;
    }

    // ---- 3. DB authorisation -----------------------------------------------
    //
    // Two distinct flows determined by whether LicenseKey is present:
    //
    //   Sign Up  (LicenseKey non-empty):
    //     1. Validate license exists and is not bound to a different HWID.
    //     2. Bind license → HWID (first use) or confirm existing binding.
    //     3. Check username is not taken by a different HWID (unique index).
    //     4. Store username + password hash for this HWID (first time) or
    //        verify they match (re-registration from same device).
    //
    //   Login  (LicenseKey empty):
    //     1. Look up account by username.
    //     2. Verify password hash.
    //     3. Verify HWID matches the device that originally signed up.
    //        Mismatch → hard reject (HWID lock).
    //
    //   Both paths finish with the full IsAuthorised() check
    //   (ban + license expiry + trusted loader hash).
    // -----------------------------------------------------------------------
    {
        std::lock_guard lk(g_dbMu);

        std::string usernameIn(hello.Username,
                               strnlen(hello.Username, sizeof(hello.Username)));
        std::string pwHashHex = HexStr(hello.PasswordHash, 32);
        std::string licenseKey(hello.LicenseKey,
                               strnlen(hello.LicenseKey, sizeof(hello.LicenseKey)));

        const bool isSignUp = !licenseKey.empty();

        if (isSignUp)
        {
            // ── Sign Up ──────────────────────────────────────────────────────
            if (usernameIn.empty() || usernameIn.size() > 31)
            {
                std::cout << "[S] Reject sign-up: missing or oversized username\n";
                SendMsg(s, MsgType::AuthFail);
                cleanup(); return;
            }

            // Username uniqueness: reject if another HWID already owns this name.
            auto existingOwner = g_db.GetUserByUsername(usernameIn);
            if (existingOwner && existingOwner->hwid_hash != s.hwid)
            {
                std::cout << std::format("[S] Reject sign-up: username '{}' taken\n", usernameIn);
                SendMsg(s, MsgType::AuthFail);
                cleanup(); return;
            }

            // Create / refresh the user row before license ops.
            g_db.TouchUser(s.hwid, now, s.loaderHash);

            // License validation and HWID binding.
            auto lic = g_db.GetLicense(licenseKey);
            if (!lic)
            {
                std::cout << std::format("[S] Reject sign-up: unknown license — {:.16}...\n",
                                          s.hwid);
                SendMsg(s, MsgType::AuthFail);
                cleanup(); return;
            }
            if (lic->hwid_hash.empty())
            {
                // Unbound license — bind atomically via DB WHERE hwid_hash=''.
                // If another thread won the race, BindLicense returns false.
                if (!g_db.BindLicense(licenseKey, s.hwid))
                {
                    std::cout << "[S] Reject sign-up: license bind race lost\n";
                    SendMsg(s, MsgType::AuthFail);
                    cleanup(); return;
                }
                g_db.SetUserLicense(s.hwid, licenseKey);
                std::cout << std::format("[S] License bound to HWID {:.16}...\n", s.hwid);
            }
            else if (lic->hwid_hash != s.hwid)
            {
                std::cout << "[S] Reject sign-up: license already bound to a different device\n";
                SendMsg(s, MsgType::AuthFail);
                cleanup(); return;
            }
            else
            {
                // Already bound to this HWID (re-registration).
                g_db.SetUserLicense(s.hwid, licenseKey);
            }

            // Store credentials first time; verify on re-registration.
            int cred = g_db.CheckOrStoreCredentials(s.hwid, usernameIn, pwHashHex);
            if (cred != 0)
            {
                std::cout << std::format("[S] Reject sign-up: credential conflict for {:.16}...\n",
                                          s.hwid);
                SendMsg(s, MsgType::AuthFail);
                cleanup(); return;
            }
        }
        else
        {
            // ── Login ─────────────────────────────────────────────────────────
            if (usernameIn.empty())
            {
                std::cout << "[S] Reject login: empty username\n";
                SendMsg(s, MsgType::AuthFail);
                cleanup(); return;
            }

            // Look up account by username.
            auto acct = g_db.GetUserByUsername(usernameIn);
            if (!acct)
            {
                std::cout << std::format("[S] Reject login: unknown username '{}'\n", usernameIn);
                SendMsg(s, MsgType::AuthFail);
                cleanup(); return;
            }

            // Verify password.
            if (acct->password_hash != pwHashHex)
            {
                std::cout << std::format("[S] Reject login: wrong password for '{}'\n", usernameIn);
                SendMsg(s, MsgType::AuthFail);
                cleanup(); return;
            }

            // HWID lock — must connect from the registered device.
            if (acct->hwid_hash != s.hwid)
            {
                std::cout << std::format(
                    "[S] Reject login: HWID mismatch for '{}' "
                    "(registered={:.16}... actual={:.16}...)\n",
                    usernameIn, acct->hwid_hash, s.hwid);
                SendMsg(s, MsgType::AuthFail);
                cleanup(); return;
            }

            // Credentials verified — update last_seen and loader hash.
            g_db.TouchUser(s.hwid, now, s.loaderHash);
        }

        // ── Final check: ban + license expiry + trusted loader hash ──────────
        if (!g_db.IsAuthorised(s.hwid, s.loaderHash, now))
        {
            auto user = g_db.GetUser(s.hwid);
            if (user && user->is_banned)
                std::cout << std::format("[S] Reject: banned ({}) — {:.16}...\n",
                                          user->ban_reason, s.hwid);
            else if (g_db.TrustedHashesEnabled() && !g_db.IsHashTrusted(s.loaderHash))
                std::cout << std::format("[S] Reject: untrusted loader hash {:.16}...\n",
                                          s.loaderHash);
            else
                std::cout << std::format("[S] Reject: license expired or missing — {:.16}...\n",
                                          s.hwid);

            SendMsg(s, MsgType::AuthFail);
            cleanup(); return;
        }
    }

    // ---- 3.5. Session cap & per-HWID duplicate session guard ----
    // Check cap first (cheap atomic load) before taking the HWID lock.
    if (g_activeSessions.load() >= MAX_CONCURRENT_SESSIONS)
    {
        std::cout << "[S] Reject: concurrent session cap reached\n";
        SendMsg(s, MsgType::AuthFail);
        cleanup(); return;
    }
    {
        bool inserted = false;
        {
            std::lock_guard lk(g_hwidMu);
            inserted = g_activeHwids.insert(s.hwid).second;
            if (inserted) hwidRegistered = true;
        }
        if (!inserted)
        {
            std::cout << std::format("[S] Reject: duplicate session for {:.16}...\n", s.hwid);
            SendMsg(s, MsgType::AuthFail);
            cleanup(); return;
        }
    }

    // ---- 4. Generate session token ----
    {
        bool tokenOk = false;
#ifdef _WIN32
        HCRYPTPROV hp{};
        if (CryptAcquireContextW(&hp, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        {
            tokenOk = CryptGenRandom(hp, sizeof(s.token), s.token) == TRUE;
            CryptReleaseContext(hp, 0);
        }
#else
        tokenOk = RAND_bytes(s.token, sizeof(s.token)) == 1;
#endif
        if (!tokenOk)
        {
            std::cout << "[S] FATAL: token generation failed — refusing connection\n";
            SendMsg(s, MsgType::AuthFail);
            cleanup(); return;
        }
    }

    // ---- 5. Send AuthOk PLAIN (hdrKey still 0) ----
    {
        AuthOkPayload aok{};
        memcpy(aok.SessionToken, s.token, 16);
        aok.ExpiresAt = (uint32_t)(now + 3600);
        if (!SendMsg(s, MsgType::AuthOk, &aok, sizeof(aok)))
        { cleanup(); return; }
    }

    // ---- 6. Derive session keys (all subsequent messages are encrypted) ----
    s.hdrKey   = DeriveKey(s.token, 0);
    s.dllKey   = DeriveKey(s.token, 1);
    s.tokenHex = HexStr(s.token, 16);

    std::cout << std::format("[S] AuthOk -> {:.16}...\n", s.hwid);
    ++g_activeSessions;

    // ---- 7. Persist session ----
    {
        std::lock_guard lk(g_dbMu);
        SessionRow sr{};
        sr.token_hex      = s.tokenHex;
        sr.hwid_hash      = s.hwid;
        sr.created_at     = now;
        sr.last_heartbeat = now;
        g_db.InsertSession(sr);
    }

    // ---- 8. Push config blob (from memory cache, loaded once at startup) ----
    if (!g_cfgBytes.empty())
    {
        if (g_cfgBytes.size() > 0xFFFF)
            std::cout << "[S] WARNING: config.bin exceeds 65535 bytes — skipping\n";
        else
            SendMsg(s, MsgType::Config, g_cfgBytes.data(), (uint16_t)g_cfgBytes.size());
    }

    // ---- 9. Session keep-alive loop ----
    // DLL is streamed on-demand: the loader sends RequestDll when the user
    // presses Inject, decoupling login from DLL delivery.  Heartbeats keep
    // the session alive while the user is on the main panel.
    static constexpr int MAX_HB_MISSES = 2;
    int hbMisses = 0;
    while (g_running.load())
    {
        MsgType mt{}; std::vector<uint8_t> mp;
        if (!RecvMsg(s, mt, mp, 35000))
        {
            ++hbMisses;
            std::cout << std::format("[S] HB miss {}/{} for {:.16}...\n",
                                      hbMisses, MAX_HB_MISSES, s.hwid);
            if (hbMisses >= MAX_HB_MISSES)
            {
                std::cout << std::format("[S] Evicting {:.16}... (missed {} heartbeats)\n",
                                          s.hwid, hbMisses);
                break;
            }
            continue;
        }
        hbMisses = 0;

        if (mt == MsgType::Heartbeat)
        {
            SendMsg(s, MsgType::Ack);
            std::lock_guard lk(g_dbMu);
            g_db.TouchSession(s.tokenHex, (int64_t)time(nullptr));
        }
        else if (mt == MsgType::RequestDll)
        {
            // User pressed Inject — stream the DLL now.
            if (!g_dllBytes.empty())
            {
                std::cout << std::format("[S] RequestDll from {:.16}...\n", s.hwid);
                if (!StreamEncryptedDll(s))
                    std::cout << std::format("[S] DLL stream aborted for {:.16}...\n", s.hwid);
                else
                    std::cout << std::format("[S] DLL delivered to {:.16}...\n", s.hwid);
            }
            else
            {
                // No DLL on server — send empty BinaryReady + BinaryEnd so the
                // loader doesn't time out waiting and stays in the session loop.
                std::cout << "[S] RequestDll — no DLL loaded; sending empty response\n";
                BinaryReadyPayload br{};  // all-zero: TotalBytes=0, NumChunks=0
                SendMsg(s, MsgType::BinaryReady, &br, sizeof(br));
                SendMsg(s, MsgType::BinaryEnd);
            }
        }
    }

    // ---- 11. Cleanup ----
    {
        std::lock_guard lk(g_dbMu);
        g_db.DeleteSession(s.tokenHex);
    }
    --g_activeSessions;
    std::cout << std::format("[S] {:.16}... disconnected (active={})\n",
                              s.hwid, g_activeSessions.load());
    cleanup();
}

// ---------------------------------------------------------------------------
// Admin console — stdin command loop
// ---------------------------------------------------------------------------
static void AdminConsole()
{
    std::cout << R"(
[Admin] Ready. Commands:
  issue <tier 1-4> <days>      Generate new license (days=0 = lifetime)
  activate <KEY> <hwid_hex>    Bind license to HWID
  extend <KEY> <days>          Extend license expiry by N days
  ban <hwid_hex> [reason]      Ban user (instant eviction on next auth)
  unban <hwid_hex>             Remove ban
  list-licenses                Dump all licenses
  list-users                   Dump all users
  purchases <hwid_hex>         Purchase history
  sessions                     Active session count
  prune                        Prune idle sessions now
  add-hash <sha256_hex> [note] Add a trusted loader hash
  rm-hash <sha256_hex>         Remove a trusted loader hash
  list-hashes                  Show all trusted loader hashes
  ban-ip <ip> [reason]         Permanently ban an IP (persisted to DB)
  unban-ip <ip>                Remove a persistent IP ban
  list-ip-bans                 Show all banned IPs
  grant-owner <hwid_hex>       Set loader user role to owner
  grant-admin <hwid_hex>       Set loader user role to admin
  demote <hwid_hex>            Set loader user role back to user
  web-owner <username>         Set web account role to owner
  web-admin <username>         Set web account role to admin
  web-demote <username>        Set web account role back to user
  web-accounts                 List all web accounts
  help                         This message
)";

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string cmd; ss >> cmd;

        if (cmd == "issue")
        {
            int tier = 1, days = 0;
            ss >> tier >> days;
            std::lock_guard lk(g_dbMu);
            std::string key = g_lm->IssueLicense(tier, days);
            if (key.empty())
                std::cout << "[Admin] Issue failed\n";
            else
                std::cout << "[Admin] " << TierName(tier) << " license: " << key << "\n";
        }
        else if (cmd == "activate")
        {
            std::string key, hwid;
            ss >> key >> hwid;
            std::lock_guard lk(g_dbMu);
            std::string err = g_lm->ActivateLicense(key, hwid, (int64_t)time(nullptr));
            if (err.empty())
                std::cout << "[Admin] Activated " << key << " -> " << hwid << "\n";
            else
                std::cout << "[Admin] Error: " << err << "\n";
        }
        else if (cmd == "extend")
        {
            // extend <KEY> <days>
            // Adds <days> days to the license's current expiry.
            // For lifetime licenses, this creates an explicit future expiry.
            std::string key; int days = 0;
            ss >> key >> days;
            if (key.empty() || days <= 0)
            {
                std::cout << "[Admin] Usage: extend <KEY> <days>\n";
            }
            else
            {
                int64_t extra = (int64_t)days * 86400LL;
                std::lock_guard lk(g_dbMu);
                if (g_db.ExtendLicense(key, extra))
                    std::cout << "[Admin] Extended " << key << " by " << days << " day(s)\n";
                else
                    std::cout << "[Admin] extend failed — key not found?\n";
            }
        }
        else if (cmd == "ban")
        {
            std::string hwid, reason;
            ss >> hwid;
            std::getline(ss >> std::ws, reason);
            std::lock_guard lk(g_dbMu);
            g_db.BanUser(hwid, reason.empty() ? "admin ban" : reason);
            std::cout << "[Admin] Banned " << hwid << "\n";
        }
        else if (cmd == "unban")
        {
            std::string hwid; ss >> hwid;
            std::lock_guard lk(g_dbMu);
            g_db.UnbanUser(hwid);
            std::cout << "[Admin] Unbanned " << hwid << "\n";
        }
        else if (cmd == "list-licenses")
        {
            std::lock_guard lk(g_dbMu);
            auto rows = g_db.ListLicenses();
            std::cout << "[Admin] " << rows.size() << " license(s):\n";
            for (auto& r : rows)
                std::cout << std::format("  {} | {} | exp={} | hwid={}\n",
                    r.key, TierName(r.tier),
                    r.expires_at == 0 ? "never" : std::to_string(r.expires_at),
                    r.hwid_hash.empty() ? "(unbound)" : r.hwid_hash.substr(0,16) + "...");
        }
        else if (cmd == "list-users")
        {
            std::lock_guard lk(g_dbMu);
            auto users = g_db.ListUsers();
            std::cout << "[Admin] " << users.size() << " user(s):\n";
            for (auto& u : users)
                std::cout << std::format(
                    "  {:.16}... | key={} | {} | last={} | ban={}\n",
                    u.hwid_hash,
                    u.license_key.size() >= 8 ? u.license_key.substr(0,8) + "..." : u.license_key,
                    u.is_banned ? "BANNED" : "ok",
                    u.last_seen,
                    u.is_banned ? u.ban_reason : "-");
        }
        else if (cmd == "purchases")
        {
            std::string hwid; ss >> hwid;
            std::lock_guard lk(g_dbMu);
            auto rows = g_db.GetPurchases(hwid);
            std::cout << "[Admin] " << rows.size() << " purchase(s) for " << hwid << ":\n";
            for (auto& r : rows)
                std::cout << std::format("  id={} key={} ${:.2f} at={} note={}\n",
                    r.id, r.license_key, r.amount_cents / 100.0, r.purchased_at, r.note);
        }
        else if (cmd == "sessions")
        {
            std::cout << "[Admin] Active sessions: " << g_activeSessions.load() << "\n";
        }
        else if (cmd == "prune")
        {
            std::lock_guard lk(g_dbMu);
            int n = g_db.PruneSessions((int64_t)time(nullptr));
            std::cout << "[Admin] Pruned " << n << " session(s)\n";
        }
        else if (cmd == "add-hash")
        {
            std::string hash, note;
            ss >> hash;
            std::getline(ss >> std::ws, note);
            std::lock_guard lk(g_dbMu);
            g_db.AddTrustedHash(hash, note);
            std::cout << "[Admin] Trusted hash added: " << hash.substr(0,16) << "...\n";
        }
        else if (cmd == "rm-hash")
        {
            std::string hash; ss >> hash;
            std::lock_guard lk(g_dbMu);
            g_db.RemoveTrustedHash(hash);
            std::cout << "[Admin] Removed hash\n";
        }
        else if (cmd == "list-hashes")
        {
            std::lock_guard lk(g_dbMu);
            auto hashes = g_db.ListHashes();
            std::cout << "[Admin] " << hashes.size() << " trusted hash(es):\n";
            for (auto& r : hashes)
                std::cout << std::format("  {} | note={} | added={}\n",
                    r.hash, r.note, r.added_at);
        }
        else if (cmd == "ban-ip")
        {
            std::string ip, reason;
            ss >> ip;
            std::getline(ss >> std::ws, reason);
            g_rl_auth.HardBanIp(ip);
            g_rl_api.HardBanIp(ip);
            g_rl_loader.HardBanIp(ip);
            std::lock_guard lk(g_dbMu);
            g_db.BanIp(ip, reason.empty() ? "manual ban" : reason);
            std::cout << "[Admin] IP banned (in-memory + DB): " << ip << "\n";
        }
        else if (cmd == "unban-ip")
        {
            std::string ip; ss >> ip;
            g_rl_auth.Unban(ip);
            g_rl_api.Unban(ip);
            g_rl_loader.Unban(ip);
            std::lock_guard lk(g_dbMu);
            g_db.UnbanIp(ip);
            std::cout << "[Admin] IP unbanned: " << ip << "\n";
        }
        else if (cmd == "list-ip-bans")
        {
            std::lock_guard lk(g_dbMu);
            auto bans = g_db.ListIpBans();
            std::cout << "[Admin] " << bans.size() << " banned IP(s):\n";
            for (auto& [ip, reason] : bans)
                std::cout << "  " << ip << " — " << reason << "\n";
        }
        else if (cmd == "grant-owner" || cmd == "grant-admin")
        {
            std::string hwid; ss >> hwid;
            if (hwid.empty()) { std::cout << "[Admin] Usage: " << cmd << " <hwid_hex>\n"; }
            else {
                std::string role = (cmd == "grant-owner") ? "owner" : "admin";
                std::lock_guard lk(g_dbMu);
                g_db.SetUserRole(hwid, role);
                std::cout << "[Admin] " << hwid << " → role=" << role << "\n";
            }
        }
        else if (cmd == "demote")
        {
            std::string hwid; ss >> hwid;
            if (hwid.empty()) { std::cout << "[Admin] Usage: demote <hwid_hex>\n"; }
            else {
                std::lock_guard lk(g_dbMu);
                g_db.SetUserRole(hwid, "user");
                std::cout << "[Admin] " << hwid << " → role=user\n";
            }
        }
        else if (cmd == "web-owner" || cmd == "web-admin")
        {
            std::string uname; ss >> uname;
            if (uname.empty()) { std::cout << "[Admin] Usage: " << cmd << " <username>\n"; }
            else {
                std::string role = (cmd == "web-owner") ? "owner" : "admin";
                std::lock_guard lk(g_dbMu);
                if (g_db.SetWebAccountRole(uname, role))
                    std::cout << "[Admin] Web account " << uname << " → role=" << role << "\n";
                else
                    std::cout << "[Admin] Web account not found: " << uname << "\n";
            }
        }
        else if (cmd == "web-demote")
        {
            std::string uname; ss >> uname;
            if (uname.empty()) { std::cout << "[Admin] Usage: web-demote <username>\n"; }
            else {
                std::lock_guard lk(g_dbMu);
                g_db.SetWebAccountRole(uname, "user");
                std::cout << "[Admin] Web account " << uname << " → role=user\n";
            }
        }
        else if (cmd == "web-accounts")
        {
            std::lock_guard lk(g_dbMu);
            auto accts = g_db.ListWebAdmins();
            std::cout << "[Admin] Web admins/owners:\n";
            for (auto& a : accts)
                std::cout << std::format("  {} | role={}\n", a.username, a.role);
        }
        else if (cmd == "help")
        {
            std::cout << "Commands: issue activate extend ban unban list-licenses list-users"
                         " purchases sessions prune add-hash rm-hash list-hashes"
                         " ban-ip unban-ip list-ip-bans grant-owner grant-admin demote"
                         " web-owner web-admin web-demote web-accounts\n";
        }
        else
        {
            std::cout << "[Admin] Unknown: " << cmd << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Maintenance thread — prunes stale sessions every 5 min + DB backup every 6 h.
// Sleeps in 1-second ticks so shutdown is immediate rather than blocked.
// ---------------------------------------------------------------------------
static void MaintenanceThread()
{
    int pruneTicks  = 0;   // fires every 300 ticks (5 min)
    int backupTicks = 0;   // fires every 21600 ticks (6 h)
    int expireTicks = 0;   // fires every 43200 ticks (12 h)

    while (g_running.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        ++pruneTicks;
        ++backupTicks;
        ++expireTicks;

        if (pruneTicks >= 300)
        {
            pruneTicks = 0;
            std::lock_guard lk(g_dbMu);
            int n = g_db.PruneSessions((int64_t)time(nullptr), 7200);
            if (n > 0) std::cout << "[S] Pruned " << n << " stale session(s)\n";
            int nw = g_db.PruneWebSessions((int64_t)time(nullptr));
            if (nw > 0) std::cout << "[S] Pruned " << nw << " expired web session(s)\n";
            g_rl_auth.Prune();
            g_rl_api.Prune();
            g_rl_loader.Prune();
        }

        if (backupTicks >= 21600)
        {
            backupTicks = 0;
            RunBackup();
        }

        if (expireTicks >= 43200)
        {
            expireTicks = 0;
            std::lock_guard lk(g_dbMu);
            int expired = g_db.PruneExpiredLicenses((int64_t)time(nullptr));
            if (expired > 0)
                std::cout << std::format(
                    "[S] License sweep: {} expired license(s) — sessions cleared\n",
                    expired);
            else
                std::cout << "[S] License sweep: all licenses valid\n";
        }
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // --gen-token: print a fresh 32-byte random hex token and exit.
    // Useful for generating both the HWID pepper and the admin secret.
    if (argc >= 2 && std::string(argv[1]) == "--gen-token")
    {
        KeyVault::GenerateToken();
        return 0;
    }

    g_startTime = (int64_t)time(nullptr);

    std::cout << "[SparkyServer] v3.2 (PostgreSQL + TLS + HWID pepper + DB backup)\n";

    if (const char* o = std::getenv(XS("SPARKY_ALLOWED_ORIGIN")))
    {
        g_allowedOrigin = o;
        std::cout << "[S] Web API origin restriction: " << g_allowedOrigin << "\n";
    }
    else
    {
        std::cout << "[S] Web API origin restriction: disabled (set SPARKY_ALLOWED_ORIGIN to restrict)\n";
        if (std::getenv("SPARKY_DEV") == nullptr)
        {
            std::cerr << "[WARN] SPARKY_ALLOWED_ORIGIN not set — CORS allows any origin. "
                         "Set this in production!\n";
        }
    }

    if (const char* k = std::getenv(XS("SPARKY_KEY")))
    {
        g_sparkyKey.Set(k);
        ClearEnv(XS("SPARKY_KEY"));
        std::cout << "[S] Loaded SPARKY_KEY from environment.\n";
    }

    if (const char* ak = std::getenv(XS("SPARKY_ATTEST_KEY")))
    {
        if (std::strlen(ak) == 64 && ParseHex(std::string(ak), g_attestKey, 32))
        {
            g_attestKeySet = true;
            std::cout << "[S] Loader attestation: HMAC-SHA256 enabled\n";
        }
        else
        {
            std::cerr << "[S] SPARKY_ATTEST_KEY must be exactly 64 hex chars — ignored\n";
        }
        ClearEnv(XS("SPARKY_ATTEST_KEY"));
    }
    else
    {
        std::cout << "[S] Loader attestation: disabled (set SPARKY_ATTEST_KEY in production)\n";
    }

    if (const char* ps = std::getenv(XS("SPARKY_PROXY_SECRET")))
    {
        g_proxySecret.Set(ps);
        ClearEnv(XS("SPARKY_PROXY_SECRET"));
        std::cout << "[S] Proxy secret: set (web API restricted to Vercel proxy)\n";
    }
    else
    {
        std::cout << "[S] Proxy secret: not set (web API open — set SPARKY_PROXY_SECRET in production)\n";
    }

    if (const char* rk = std::getenv(XS("RESEND_API_KEY")))
    {
        g_resendKey.Set(rk);
        ClearEnv(XS("RESEND_API_KEY"));
        std::cout << "[S] Resend email: enabled\n";
    }
    else
    {
        std::cout << "[S] Resend email: disabled (set RESEND_API_KEY to enable)\n";
    }
    if (const char* rf = std::getenv(XS("RESEND_FROM_EMAIL")))
    {
        g_resendFrom.Set(rf);
        ClearEnv(XS("RESEND_FROM_EMAIL"));
    }

    // ---- GCP Cloud Run: honour $PORT env var (default 8080 on Cloud Run) ----
    // If PORT is set (e.g. by Cloud Run), listen on that port instead of 7777.
    if (const char* portEnv = std::getenv(XS("PORT")))
    {
        int p = std::atoi(portEnv);
        if (p > 0 && p <= 65535)
        {
            LISTEN_PORT = static_cast<uint16_t>(p);
            std::cout << std::format("[S] PORT env override: listening on {}\n", LISTEN_PORT);
        }
    }

    // ---- Load PostgreSQL connection string ----
    try
    {
        std::string _connstr = KeyVault::LoadConnStr();
        g_connstr.Set(_connstr);
        OPENSSL_cleanse(_connstr.data(), _connstr.size());
        // KeyVault already calls ClearEnv internally if reading from env var
    }
    catch (const std::exception& e)
    {
        std::cerr << "[S] FATAL: " << e.what() << "\n";
        return 1;
    }

    // ---- Load HWID pepper (optional) ----
    if (const char* p = std::getenv(XS("SPARKY_HWID_PEPPER")))
    {
        std::string ps(p);
        if (ps.size() == 64)
        {
            auto h2 = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
                if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
                if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
                return 0;
            };
            for (int i = 0; i < 32; ++i)
                g_hwidPepper[i] = (h2(ps[i*2]) << 4) | h2(ps[i*2+1]);
            g_pepperSet = true;
            ClearEnv(XS("SPARKY_HWID_PEPPER"));
            std::cout << "[S] HWID pepper: loaded\n";
        }
        else
            std::cout << "[S] WARNING: SPARKY_HWID_PEPPER must be 64 hex chars — pepper disabled\n";
    }
    else
    {
        std::cout << "[S] HWID pepper: disabled (set SPARKY_HWID_PEPPER=<64 hex chars> to enable)\n";
    }

    // ---- Open database ----
    {
        std::lock_guard lk(g_dbMu);
        std::string _dbc = g_connstr.Get();
        bool dbOk = g_db.Open(_dbc);
        OPENSSL_cleanse(_dbc.data(), _dbc.size());
        if (!dbOk)
        { std::cerr << "[S] FATAL: cannot connect to PostgreSQL\n"; return 1; }
        std::cout << "[S] PostgreSQL: connected\n";
        if (g_db.TrustedHashesEnabled())
            std::cout << "[S] Loader integrity check: ENABLED\n";
        else
            std::cout << "[S] Loader integrity check: disabled (add-hash to enable)\n";

        // Restore persistent IP bans into the in-memory rate limiter so they
        // survive server restarts without needing iptables.
        auto ipBans = g_db.ListIpBans();
        for (auto& [ip, reason] : ipBans)
        {
            g_rl_loader.HardBanIp(ip);
            g_rl_auth.HardBanIp(ip);
            g_rl_api.HardBanIp(ip);
        }
        if (!ipBans.empty())
            std::cout << "[S] Restored " << ipBans.size() << " persistent IP ban(s)\n";
    }

    LicenseManager lm(g_db);
    g_lm = &lm;

    g_dllBytes = ReadFileFull(DLL_FILE);
    if (g_dllBytes.empty())
        std::cout << "[S] WARNING: " << DLL_FILE << " not found\n";
    else
        std::cout << std::format("[S] DLL loaded ({} bytes)\n", g_dllBytes.size());

    g_cfgBytes = ReadFileFull(CONFIG_FILE);
    if (!g_cfgBytes.empty())
        std::cout << std::format("[S] Config loaded ({} bytes)\n", g_cfgBytes.size());

    // ---- TLS init (optional) ----
    // Place sparky.crt + sparky.key next to the binary to enable TLS.
    // Generate a self-signed cert for testing:
    //   openssl req -x509 -newkey rsa:4096 -keyout sparky.key -out sparky.crt \
    //               -days 365 -nodes -subj "/CN=sparky"
    //
    // Set SPARKY_NO_TLS=1 to skip TLS entirely (Cloud Run: GFE terminates TLS externally).
    OPENSSL_init_ssl(0, nullptr);
    if (getenv("SPARKY_NO_TLS"))
    {
        std::cout << "[S] TLS: disabled via SPARKY_NO_TLS (plaintext mode)\n";
    }
    else
    {
    g_sslCtx = SSL_CTX_new(TLS_server_method());
    if (g_sslCtx)
    {
        if (SSL_CTX_use_certificate_file(g_sslCtx, CERT_FILE, SSL_FILETYPE_PEM) == 1
            && SSL_CTX_use_PrivateKey_file(g_sslCtx, KEY_FILE,  SSL_FILETYPE_PEM) == 1)
        {
            std::cout << std::format("[S] TLS: enabled ({} / {})\n", CERT_FILE, KEY_FILE);
        }
        else
        {
            SSL_CTX_free(g_sslCtx);
            g_sslCtx = nullptr;

            // Plaintext is only permitted in explicit dev mode.
            // In production SPARKY_ALLOW_PLAINTEXT must NOT be set.
            const bool devMode = (getenv("SPARKY_ALLOW_PLAINTEXT") != nullptr);
            if (!devMode)
            {
                std::cerr << "[S] FATAL: TLS cert/key not found.\n"
                          << "[S]        Expected: " << CERT_FILE
                          << " / " << KEY_FILE << "\n"
                          << "[S]        Generate with:\n"
                          << "[S]          openssl req -x509 -newkey rsa:4096 "
                             "-keyout sparky.key -out sparky.crt "
                             "-days 365 -nodes -subj /CN=sparky\n"
                          << "[S]        Set SPARKY_ALLOW_PLAINTEXT=1 only for "
                             "local dev/testing.\n";
#ifdef _WIN32
                WSACleanup();
#endif
                return 1;
            }
            std::cout << "[S] TLS: cert/key not found — plaintext dev mode "
                         "(SPARKY_ALLOW_PLAINTEXT=1)\n";
        }
    }
    } // end !SPARKY_NO_TLS

#ifdef _WIN32
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
#else
    signal(SIGINT,  PosixSignalHandler);
    signal(SIGTERM, PosixSignalHandler);
    signal(SIGPIPE, SIG_IGN);  // Prevent crash when writing to a closed socket
#endif

    std::thread(MaintenanceThread).detach();
    if (ENABLE_ADMIN_CONSOLE)
        std::thread(AdminConsole).detach();

#ifdef _WIN32
    WSADATA wsa{}; WSAStartup(MAKEWORD(2,2), &wsa);
#endif

#ifdef _WIN32
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(LISTEN_PORT);
    a.sin_addr.s_addr=INADDR_ANY;
    int opt=1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    if (bind(ls, (sockaddr*)&a, sizeof(a)) != 0 || listen(ls, SOMAXCONN) != 0)
    { std::cerr << "[S] Failed to bind :" << LISTEN_PORT << "\n"; return 1; }
    std::cout << std::format("[S] Listening on :{}{}\n",
                              LISTEN_PORT, g_sslCtx ? " (TLS)" : " (plaintext)");

    while (g_running.load())
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(ls, &readfds);
        timeval tv{1, 0};
#ifdef _WIN32
        if (select(0, &readfds, nullptr, nullptr, &tv) <= 0)
#else
        if (select(ls + 1, &readfds, nullptr, nullptr, &tv) <= 0)
#endif
            continue;

        sockaddr_in ca{}; socklen_t cl = sizeof(ca);
#ifdef _WIN32
        SOCKET cs = accept(ls, (sockaddr*)&ca, (int*)&cl);
        if (cs == INVALID_SOCKET) continue;
#else
        int cs = accept(ls, (sockaddr*)&ca, &cl);
        if (cs < 0) continue;
#endif

        char ip[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof(ip));

        // Skip rate-limiting for GCP internal IPs (169.254.x.x link-local) used
        // by Cloud Run GFE health probes — banning these breaks all HTTP traffic.
        const bool isGcpInternal = (strncmp(ip, "169.254.", 8) == 0);

        if (!isGcpInternal && !g_rl_loader.Allow(ip))
        {
            // If the rate limiter just triggered a new hard-ban, persist it so it
            // survives restarts (HardBanned() includes both old and new bans).
            {
                std::lock_guard lk(g_dbMu);
                g_db.BanIp(ip, "rate-limit auto-ban");
            }
#ifdef _WIN32
            closesocket(cs);
#else
            ::close(cs);
#endif
            continue;
        }

        // Per-IP concurrent connection cap — reject if already at 20 open sockets.
        {
            std::lock_guard lk(g_connMu);
            if (g_connCount[ip] >= 20)
            {
#ifdef _WIN32
                closesocket(cs);
#else
                ::close(cs);
#endif
                continue;
            }
            g_connCount[ip]++;
        }

        // Set TCP options:
        //   TCP_NODELAY — disable Nagle; heartbeat acks are tiny and latency-sensitive.
        //   SO_KEEPALIVE — detect dead clients at the TCP level (zombie cleanup).
        {
            int flag = 1;
            setsockopt(cs, IPPROTO_TCP, TCP_NODELAY,  (char*)&flag, sizeof(flag));
            setsockopt(cs, SOL_SOCKET,  SO_KEEPALIVE, (char*)&flag, sizeof(flag));
        }

        // Protocol Sniffing: Peek at the first byte to decide between TLS and Plaintext.
        // This allows HTTPS Load Balancer health checks (which often send plain HTTP internally)
        // to succeed while still supporting TLS for the actual loader connection.
        unsigned char firstByte = 0;
#ifdef _WIN32
        int peeked = recv(cs, (char*)&firstByte, 1, MSG_PEEK);
#else
        int peeked = recv(cs, &firstByte, 1, MSG_PEEK | MSG_DONTWAIT);
#endif

        SSL* ssl = nullptr;
        bool useTls = (peeked > 0 && firstByte == 0x16); // 0x16 is TLS Handshake record

        if (g_sslCtx && useTls)
        {
            ssl = SSL_new(g_sslCtx);
            SSL_set_fd(ssl, (int)cs);
            if (SSL_accept(ssl) <= 0)
            {
                std::cout << std::format("[S] TLS handshake failed from {} — {}\n",
                                          ip, TlsLastError());
                SSL_free(ssl);
#ifdef _WIN32
                closesocket(cs);
#else
                ::close(cs);
#endif
                continue;
            }
        }

        std::cout << std::format("[S] Connection from {}{}\n",
                                  ip, ssl ? " (TLS)" : (useTls ? " (Broken TLS)" : " (Plain)"));
        std::thread(HandleClient, cs, ssl, std::string(ip)).detach();
    }

    std::cout << "[S] Shutting down...\n";
#ifdef _WIN32
    closesocket(ls);
#else
    ::close(ls);
#endif
    {
        std::lock_guard lk(g_dbMu);
        g_db.Close();
    }
    if (g_sslCtx) { SSL_CTX_free(g_sslCtx); g_sslCtx = nullptr; }
#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "[S] Clean shutdown complete.\n";
    return 0;
}
