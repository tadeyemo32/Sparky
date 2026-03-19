// SparkyServer — auth + license DB + heartbeat-gated DLL streaming
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <wincrypt.h>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>
#include <format>

// OpenSSL TLS — optional at runtime.
// Place sparky.crt + sparky.key next to the binary to enable TLS.
// If absent the server runs in plaintext mode (dev mode).
#include <openssl/ssl.h>
#include <openssl/err.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")
// OpenSSL import libs are linked via CMake (OpenSSL::SSL, OpenSSL::Crypto)

#include "../include/Database.h"
#include "../include/LicenseManager.h"
#include "../include/RateLimiter.h"
#include "../include/KeyVault.h"
#include "../../SparkyLoader/include/Protocol.h"
#include "../../SparkyLoader/include/TlsLayer.h"

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
static constexpr uint16_t LISTEN_PORT   = 7777;
static constexpr uint32_t CURRENT_BUILD = 0x0001'0000;
static constexpr uint32_t CHUNK_SIZE    = 4096;

// How many DLL chunks to send between mandatory client heartbeats.
// At 4 KB/chunk, 8 chunks = 32 KB per heartbeat interval.
// If the client goes silent for HEARTBEAT_DEADLINE_MS after its batch,
// the server drops the connection mid-stream.
static constexpr uint32_t CHUNKS_PER_HEARTBEAT = 8;

static constexpr const char* DLL_FILE    = "SparkyCore.dll";
static constexpr const char* CONFIG_FILE = "config.bin";
static constexpr const char* CERT_FILE   = "sparky.crt";
static constexpr const char* KEY_FILE    = "sparky.key";
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

// Connection string stored globally for RunBackup()
static std::string g_connstr;

static BOOL WINAPI CtrlHandler(DWORD)
{
    std::cout << "[S] Shutdown signal received — stopping accept loop...\n";
    g_running = false;
    return TRUE;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string HexStr(const uint8_t* d, size_t n)
{
    static const char h[] = "0123456789abcdef";
    std::string out; out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { out += h[d[i]>>4]; out += h[d[i]&0xF]; }
    return out;
}

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
    HCRYPTPROV hProv{};
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return hwid; // fallback to unpepered on CryptAPI failure

    uint8_t digest[32]{};
    bool ok = false;
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
    return ok ? HexStr(digest, 32) : hwid;
}

// ---------------------------------------------------------------------------
// RunBackup — dump the PostgreSQL database via pg_dump.
// Writes to backups/sparky_YYYYMMDD_HHMMSS.sql next to the binary.
// pg_dump must be in PATH (it ships with the PostgreSQL client tools).
// Called from MaintenanceThread every 6 hours.
// ---------------------------------------------------------------------------
static void RunBackup()
{
    if (g_connstr.empty()) return;

    // Ensure backups/ directory exists (no-op if already there)
    CreateDirectoryA("backups", nullptr);

    // Build timestamp filename
    time_t now = time(nullptr);
    tm* t = localtime(&now);
    char fname[80]{};
    snprintf(fname, sizeof(fname),
             "backups/sparky_%04d%02d%02d_%02d%02d%02d.sql",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);

    // Build command: pg_dump -d "<connstr>" -f "<fname>"
    // Quotes around the connstr handle spaces in password / host values.
    std::string cmd = std::string("pg_dump -d \"") + g_connstr + "\" -f \"" + fname + "\"";

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
        std::cout << std::format("[S] DB backup → {}\n", fname);
    else
        std::cout << std::format("[S] pg_dump exited {} — check PATH / credentials\n", exitCode);
}

// ---------------------------------------------------------------------------
// Per-client session state
// ---------------------------------------------------------------------------
struct ClientSession
{
    SOCKET      sock;
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
                    std::vector<uint8_t>& pay, DWORD ms = 10000)
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
static void HandleClient(SOCKET csock, SSL* ssl)
{
    ClientSession s{};
    s.sock = csock;
    s.ssl  = ssl;
    const int64_t now = (int64_t)time(nullptr);

    // Shared cleanup: TLS teardown then socket close.
    // Called at every early return and at the end of the function.
    auto cleanup = [&]() {
        if (s.ssl) { SSL_shutdown(s.ssl); SSL_free(s.ssl); s.ssl = nullptr; }
        closesocket(csock);
    };

    // ---- 1. Receive Hello ----
    MsgHeader h{};
    if (!NetRecv(s.sock, s.ssl, &h, sizeof(h), 10000)
        || h.Magic   != PROTO_MAGIC
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
    std::vector<uint8_t> pay(h.Length);
    if (!NetRecv(s.sock, s.ssl, pay.data(), h.Length, 10000)) { cleanup(); return; }
    uint32_t crc{}; if (!NetRecv(s.sock, s.ssl, &crc, 4, 10000)) { cleanup(); return; }

    // Verify Hello CRC
    {
        uint32_t computed = Crc32((uint8_t*)&h, sizeof(h));
        if (!pay.empty()) computed ^= Crc32(pay.data(), (uint32_t)pay.size());
        if (computed != crc) { cleanup(); return; }
    }

    if (pay.size() < sizeof(HelloPayload)) { cleanup(); return; }
    const auto& hello = *reinterpret_cast<HelloPayload*>(pay.data());

    // Raw HWID hex from the loader
    s.hwid       = HexStr(hello.HwidHash,   32);
    s.loaderHash = HexStr(hello.LoaderHash, 32);

    // Apply server-side pepper before any DB lookup or storage
    if (g_pepperSet) s.hwid = PepperHwid(s.hwid);

    std::cout << std::format("[S] Hello HWID={:.16}... build={:08X} loader={:.16}...\n",
                              s.hwid, hello.BuildId, s.loaderHash);

    // ---- 2. Build ID check ----
    if (hello.BuildId != CURRENT_BUILD)
    {
        std::cout << "[S] Reject: stale build\n";
        SendMsg(s, MsgType::AuthFail);
        cleanup(); return;
    }

    // ---- 3. DB authorisation (ban + license + integrity check) ----
    {
        std::lock_guard lk(g_dbMu);
        g_db.TouchUser(s.hwid, now, s.loaderHash);

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

    // ---- 4. Generate session token via CryptGenRandom ----
    {
        HCRYPTPROV hp{};
        bool tokenOk = false;
        if (CryptAcquireContextW(&hp, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        {
            tokenOk = CryptGenRandom(hp, sizeof(s.token), s.token) == TRUE;
            CryptReleaseContext(hp, 0);
        }
        if (!tokenOk)
        {
            std::cout << "[S] FATAL: CryptGenRandom failed — refusing connection\n";
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

    // ---- 8. Push config blob (optional) ----
    auto cfg = ReadFileFull(CONFIG_FILE);
    if (!cfg.empty())
    {
        if (cfg.size() > 0xFFFF)
            std::cout << "[S] WARNING: config.bin exceeds 65535 bytes — skipping\n";
        else
            SendMsg(s, MsgType::Config, cfg.data(), (uint16_t)cfg.size());
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
            std::cout << "[Admin] (use admin.py or add ListHashes() to Database)\n";
        }
        else if (cmd == "help")
        {
            std::cout << "Commands: issue activate ban unban list-licenses list-users"
                         " purchases sessions prune add-hash rm-hash list-hashes\n";
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

    while (g_running.load())
    {
        Sleep(1000);
        ++pruneTicks;
        ++backupTicks;

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

    std::cout << "[SparkyServer] v3.1 (PostgreSQL + TLS + HWID pepper + DB backup)\n";

    // ---- Load PostgreSQL connection string ----
    try { g_connstr = KeyVault::LoadConnStr(); }
    catch (const std::exception& e)
    {
        std::cerr << "[S] FATAL: " << e.what() << "\n";
        return 1;
    }

    // ---- Load HWID pepper (optional) ----
    if (const char* p = std::getenv("SPARKY_HWID_PEPPER"))
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
    }

    LicenseManager lm(g_db);
    g_lm = &lm;

    g_dllBytes = ReadFileFull(DLL_FILE);
    if (g_dllBytes.empty())
        std::cout << "[S] WARNING: " << DLL_FILE << " not found\n";
    else
        std::cout << std::format("[S] DLL loaded ({} bytes)\n", g_dllBytes.size());

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
                WSACleanup();
                return 1;
            }
            std::cout << "[S] TLS: cert/key not found — plaintext dev mode "
                         "(SPARKY_ALLOW_PLAINTEXT=1)\n";
        }
    }

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    std::thread(MaintenanceThread).detach();
    if (ENABLE_ADMIN_CONSOLE)
        std::thread(AdminConsole).detach();

    WSADATA wsa{}; WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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
        if (select(0, &readfds, nullptr, nullptr, &tv) <= 0)
            continue;

        sockaddr_in ca{}; int cl = sizeof(ca);
        SOCKET cs = accept(ls, (sockaddr*)&ca, &cl);
        if (cs == INVALID_SOCKET) continue;

        char ip[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof(ip));

        if (!g_rl.Allow(ip))
        {
            closesocket(cs);
            continue;
        }

        // Perform TLS handshake before spawning the client thread.
        // The thread takes full ownership of cs and ssl (may be nullptr).
        SSL* ssl = nullptr;
        if (g_sslCtx)
        {
            ssl = SSL_new(g_sslCtx);
            SSL_set_fd(ssl, (int)cs);
            if (SSL_accept(ssl) <= 0)
            {
                std::cout << std::format("[S] TLS handshake failed from {} — {}\n",
                                          ip, TlsLastError());
                SSL_free(ssl);
                closesocket(cs);
                continue;
            }
        }

        std::cout << std::format("[S] Connection from {}{}\n",
                                  ip, ssl ? " (TLS)" : "");
        std::thread(HandleClient, cs, ssl).detach();
    }

    std::cout << "[S] Shutting down...\n";
    closesocket(ls);
    {
        std::lock_guard lk(g_dbMu);
        g_db.Close();
    }
    if (g_sslCtx) { SSL_CTX_free(g_sslCtx); g_sslCtx = nullptr; }
    WSACleanup();
    std::cout << "[S] Clean shutdown complete.\n";
    return 0;
}
