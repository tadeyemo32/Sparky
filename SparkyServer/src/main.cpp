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

// OpenSSL import libs are linked via CMake (OpenSSL::SSL, OpenSSL::Crypto)

#include "../include/Database.h"
#include "../include/LicenseManager.h"
#include "../include/RateLimiter.h"
#include "../include/KeyVault.h"
#include "../include/XorStr.h"
#include "../../SparkyLoader/user/include/Protocol.h"
#include "../../SparkyLoader/user/include/TlsLayer.h"

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

// Rate limiter: 10 connections per 60 s → soft throttle
//               30 connections per 60 s → hard ban + iptables suggestion
static RateLimiter g_rl(10, 30, 60);

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
static std::string g_sparkyKey;

// Connection string stored globally for RunBackup()
static std::string g_connstr;

// Per-HWID concurrent session guard.
// Prevents one user from opening multiple simultaneous sessions.
// Protected by g_hwidMu (separate from g_dbMu to avoid deadlocks).
static std::unordered_set<std::string> g_activeHwids;
static std::mutex                       g_hwidMu;

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
    if (!g_pepperSet || hwid.size() < 64) return hwid;

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
    std::string cmd = std::string(XS("pg_dump -d \"")) + g_connstr + XS("\" -f \"") + fname + XS("\"");

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
        const char* args[] = { "pg_dump", "-d", g_connstr.c_str(), "-f", fname, nullptr };
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
    return NetSend(s.sock, s.ssl, &h, sizeof(h))
        && (buf.empty() || NetSend(s.sock, s.ssl, buf.data(), (int)buf.size()))
        && NetSend(s.sock, s.ssl, &crc, 4);
}

static bool RecvMsg(ClientSession& s, MsgType& t,
                    std::vector<uint8_t>& pay, unsigned int ms = 10000)
{
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
static void HandleClient(SOCKET csock, SSL* ssl)
#else
static void HandleClient(int csock, SSL* ssl)
#endif
{
    ClientSession s{};
    s.sock = csock;
    s.ssl  = ssl;
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
    };

    // ---- 1. Receive Hello (or HTTP Health Check) ----
    MsgHeader h{};
    std::vector<uint8_t> pay;
    if (!NetRecv(s.sock, s.ssl, &h, sizeof(h), 10000))
    {
        cleanup(); return;
    }

    // Check if it's an HTTP request (Load Balancer Health Check or curl)
    if (memcmp(&h.Magic, "GET ", 4) == 0 || memcmp(&h.Magic, "HEAD", 4) == 0)
    {
        std::string req;
        req.append((const char*)&h, sizeof(h));
        char c;
        // Read until \r\n\r\n or max 4KB
        while (req.size() < 4096 && NetRecv(s.sock, s.ssl, &c, 1, 5000))
        {
            req += c;
            if (req.size() >= 4 && req.compare(req.size() - 4, 4, "\r\n\r\n") == 0)
                break;
        }

        bool authorized = g_sparkyKey.empty();
        std::string authHex;

        if (true) // always parse headers if it's HTTP
        {
            std::string lowerReq = req;
            for (auto& ch : lowerReq) ch = (char)std::tolower((unsigned char)ch);

            // 1. Check Cloud Armor / Secret Key
            if (!g_sparkyKey.empty())
            {
                size_t pos = lowerReq.find(XS("x-sparky-key:"));
                if (pos != std::string::npos)
                {
                    size_t start = pos + 13;
                    while (start < req.size() && (req[start] == ' ' || req[start] == '\t')) start++;
                    size_t end = start;
                    while (end < req.size() && req[end] != '\r' && req[end] != '\n') end++;

                    std::string submittedKey = req.substr(start, end - start);
                    if (submittedKey.size() == g_sparkyKey.size()) {
                        volatile uint8_t diff = 0;
                        for (size_t i = 0; i < submittedKey.size(); ++i) diff |= (uint8_t)(submittedKey[i] ^ g_sparkyKey[i]);
                        if (diff == 0) authorized = true;
                    }
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
        }

        if (authorized)
        {
            // If the loader sent an auth header, we treat this as the 'Hello' phase.
            if (!authHex.empty())
            {
                HelloPayload hp{};
                if (ParseHex(authHex, reinterpret_cast<uint8_t*>(&hp), sizeof(hp)))
                {
                    std::cout << "[S] HTTP Auth detected (Loader handshake via LB)\n";
                    // Send HTTP 200 OK immediately so the LB is happy, 
                    // then handle the rest of the protocol on the same socket.
                    std::string ok = XS("HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n");
                    NetSend(s.sock, s.ssl, ok.c_str(), (int)ok.size());
                    
                    // Now jump to the 'Process Hello' logic with our parsed HelloPayload
                    // We need to wrap this in a way that the existing logic can consume it.
                    std::vector<uint8_t> fakePay(reinterpret_cast<uint8_t*>(&hp), reinterpret_cast<uint8_t*>(&hp) + sizeof(hp));
                    // Note: This requires refactoring HandleClient slightly or just copying logic.
                    // For now, I'll copy the logic below but it's cleaner to use a helper.
                    
                    // [REFACTORED AUTH LOGIC INJECTED HERE]
                    // (I will call a helper function or labels)
                    pay = std::move(fakePay); // Use the fake payload for authentication
                    goto AUTH_LOGIC_START; // Jump to shared authentication logic
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
    uint32_t crc{}; if (!NetRecv(s.sock, s.ssl, &crc, 4, 10000)) { cleanup(); return; }

    // Verify Hello CRC
    {
        uint32_t computed = Crc32((uint8_t*)&h, sizeof(h));
        if (!pay.empty()) computed ^= Crc32(pay.data(), (uint32_t)pay.size());
        if (computed != crc) { cleanup(); return; }
    }

    if (pay.size() < sizeof(HelloPayload)) { cleanup(); return; }
AUTH_LOGIC_START:
    const auto& hello = *reinterpret_cast<HelloPayload*>(pay.data());

    // Raw HWID hex from the loader
    s.hwid       = HexStr(hello.HwidHash,   32);
    s.loaderHash = HexStr(hello.LoaderHash, 32);

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

    // ---- 3. DB authorisation (ban + license + integrity check) ----
    {
        std::lock_guard lk(g_dbMu);
        g_db.TouchUser(s.hwid, now, s.loaderHash);

        // Validate the license key supplied by the loader and bind it to this HWID.
        std::string licenseKey(hello.LicenseKey,
                               strnlen(hello.LicenseKey, sizeof(hello.LicenseKey)));
        if (licenseKey.empty())
        {
            std::cout << std::format("[S] Reject: missing license key — {:.16}...\n", s.hwid);
            SendMsg(s, MsgType::AuthFail);
            cleanup(); return;
        }
        {
            auto lic = g_db.GetLicense(licenseKey);
            if (!lic)
            {
                std::cout << std::format("[S] Reject: unknown license key — {:.16}...\n", s.hwid);
                SendMsg(s, MsgType::AuthFail);
                cleanup(); return;
            }
            if (lic->hwid_hash.empty())
            {
                // Unbound license: bind it to this HWID on first use.
                g_db.BindLicense(licenseKey, s.hwid);
                g_db.SetUserLicense(s.hwid, licenseKey);
            }
            else if (lic->hwid_hash != s.hwid)
            {
                std::cout << std::format("[S] Reject: license already bound to different HWID\n");
                SendMsg(s, MsgType::AuthFail);
                cleanup(); return;
            }
            else
            {
                // Already bound to this HWID — ensure the user row reflects it.
                g_db.SetUserLicense(s.hwid, licenseKey);
            }
        }

        // Validate username + password hash (stores on first login).
        {
            std::string usernameIn(hello.Username,
                                   strnlen(hello.Username, sizeof(hello.Username)));
            std::string pwHashHex = HexStr(hello.PasswordHash, 32);
            int cred = g_db.CheckOrStoreCredentials(s.hwid, usernameIn, pwHashHex);
            if (cred != 0)
            {
                std::cout << std::format("[S] Reject: bad credentials for {:.16}...\n", s.hwid);
                SendMsg(s, MsgType::AuthFail);
                cleanup(); return;
            }
        }

        if (!g_db.IsAuthorised(s.hwid, s.loaderHash, now))
        {
            auto user = g_db.GetUser(s.hwid);
            if (user && user->is_banned)
                std::cout << std::format("[S] Reject: banned ({}) — {:.16}...\n",
                                          user->ban_reason, s.hwid);
            else if (g_db.TrustedHashesEnabled()
                     && !g_db.IsHashTrusted(s.loaderHash))
                std::cout << std::format("[S] Reject: untrusted loader hash {:.16}...\n",
                                          s.loaderHash);
            else
                std::cout << std::format("[S] Reject: no valid license — {:.16}...\n",
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
        std::lock_guard lk(g_hwidMu);
        if (g_activeHwids.count(s.hwid))
        {
            std::cout << std::format("[S] Reject: duplicate session for {:.16}...\n", s.hwid);
            SendMsg(s, MsgType::AuthFail);
            cleanup(); return;
        }
        g_activeHwids.insert(s.hwid);
        hwidRegistered = true;
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

    // ---- 9. Heartbeat-gated DLL stream ----
    if (!g_dllBytes.empty())
    {
        if (!StreamEncryptedDll(s))
            std::cout << std::format("[S] DLL stream aborted for {:.16}...\n", s.hwid);
        else
            std::cout << std::format("[S] DLL delivered to {:.16}...\n", s.hwid);
    }
    else
    {
        std::cout << "[S] No DLL loaded\n";
    }

    // ---- 10. Post-delivery keep-alive loop ----
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
            g_rl.HardBanIp(ip);
            std::lock_guard lk(g_dbMu);
            g_db.BanIp(ip, reason.empty() ? "manual ban" : reason);
            std::cout << "[Admin] IP banned (in-memory + DB): " << ip << "\n";
        }
        else if (cmd == "unban-ip")
        {
            std::string ip; ss >> ip;
            g_rl.Unban(ip);
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
        else if (cmd == "help")
        {
            std::cout << "Commands: issue activate extend ban unban list-licenses list-users"
                         " purchases sessions prune add-hash rm-hash list-hashes"
                         " ban-ip unban-ip list-ip-bans\n";
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
            g_rl.Prune();
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

    std::cout << "[SparkyServer] v3.2 (PostgreSQL + TLS + HWID pepper + DB backup)\n";

    if (const char* k = std::getenv(XS("SPARKY_KEY")))
    {
        g_sparkyKey = k;
        std::cout << "[S] Loaded SPARKY_KEY from environment.\n";
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
    try { g_connstr = KeyVault::LoadConnStr(); }
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
        if (!g_db.Open(g_connstr))
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
            g_rl.HardBanIp(ip);
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
    OPENSSL_init_ssl(0, nullptr);
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

        if (!g_rl.Allow(ip))
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
        std::thread(HandleClient, cs, ssl).detach();
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
