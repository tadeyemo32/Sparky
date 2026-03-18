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

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")

#include "../include/Database.h"
#include "../include/LicenseManager.h"
#include "../include/RateLimiter.h"
#include "../include/KeyVault.h"
#include "../../SparkyLoader/include/Protocol.h"

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
static constexpr uint16_t LISTEN_PORT   = 7777;
static constexpr uint32_t CURRENT_BUILD = 0x0001'0000;
static constexpr uint32_t CHUNK_SIZE    = 4096;

// PostgreSQL connection string loaded from KeyVault::LoadConnStr() at startup.
// Set SPARKY_PG_CONNSTR or place sparky.connstr next to the binary.
// Example: host=localhost port=5432 dbname=sparky user=sparky password=s3cr3t sslmode=require

// How many DLL chunks to send between mandatory client heartbeats.
// At 4 KB/chunk, 8 chunks = 32 KB per heartbeat interval.
// If the client goes silent for HEARTBEAT_DEADLINE_MS after its batch,
// the server drops the connection mid-stream — the client can't "dump" the
// full DLL by just reading the socket without responding.
static constexpr uint32_t CHUNKS_PER_HEARTBEAT = 8;

static constexpr const char* DLL_FILE    = "SparkyCore.dll";
static constexpr const char* CONFIG_FILE = "config.bin";
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
// Per-client session state
// ---------------------------------------------------------------------------
struct ClientSession
{
    SOCKET      sock;
    uint64_t    hdrKey   = 0;
    uint64_t    dllKey   = 0;
    uint8_t     token[16]{};
    std::string hwid;
    std::string tokenHex;
    std::string loaderHash; // hex of loader SHA-256 from Hello
};

static auto RawSend = [](SOCKET s, const void* d, int n) -> bool {
    const char* p = (const char*)d; int sent = 0;
    while (sent < n) { int r = send(s, p+sent, n-sent, 0); if (r<=0) return false; sent+=r; }
    return true;
};
static auto RawRecv = [](SOCKET s, void* d, int n, DWORD ms = 10000) -> bool {
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&ms, sizeof(ms));
    char* p = (char*)d; int got = 0;
    while (got < n) { int r = recv(s, p+got, n-got, 0); if (r<=0) return false; got+=r; }
    return true;
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
    return RawSend(s.sock, &h, sizeof(h))
        && (buf.empty() || RawSend(s.sock, buf.data(), (int)buf.size()))
        && RawSend(s.sock, &crc, 4);
}

static bool RecvMsg(ClientSession& s, MsgType& t,
                    std::vector<uint8_t>& pay, DWORD ms = 10000)
{
    MsgHeader h{};
    if (!RawRecv(s.sock, &h, sizeof(h), ms)) return false;
    if (h.Magic != PROTO_MAGIC || h.Version != PROTO_VERSION) return false;
    pay.resize(h.Length);
    if (h.Length && !RawRecv(s.sock, pay.data(), h.Length, ms)) return false;
    uint32_t rc{}; if (!RawRecv(s.sock, &rc, 4, ms)) return false;
    uint32_t lc = Crc32((uint8_t*)&h, sizeof(h));
    if (!pay.empty()) lc ^= Crc32(pay.data(), (uint32_t)pay.size());
    if (lc != rc) return false;
    if (s.hdrKey && !pay.empty()) XorStream(pay.data(), (uint32_t)pay.size(), s.hdrKey);
    t = h.Type; return true;
}

// ---------------------------------------------------------------------------
// StreamEncryptedDll — heartbeat-gated, rolling-key chunked DLL delivery.
//
// Protocol (mirrors loader's receive loop exactly):
//   Server sends BinaryReady { total, chunkSize, numChunks, chunksPerHB }
//   For each batch of chunksPerHB chunks (and the final partial batch):
//     Server sends N × BinaryChunk, each encrypted with current rollingKey
//     Server waits up to HEARTBEAT_DEADLINE_MS for a HeartbeatPayload (nonce)
//     Both sides call RollKey(rollingKey, nonce) to advance to the next key
//     Server sends Ack
//   Server sends BinaryEnd
//
// Security properties:
//   • Each 32 KB batch uses a different key — static packet capture is useless
//   • Missing/wrong heartbeat → key divergence → DLL decrypts as garbage
//   • No pre-buffering needed — chunks are encrypted on-the-fly from g_dllBytes
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

    uint64_t rollingKey = s.dllKey; // initial key; rolled after each HB batch

    for (uint32_t c = 0; c < nChunks; ++c)
    {
        const uint32_t off  = c * CHUNK_SIZE;
        const uint32_t size = (uint32_t)std::min((size_t)CHUNK_SIZE,
                                                   (size_t)(total - off));

        // Copy raw plaintext chunk, encrypt with current rolling key
        std::vector<uint8_t> chunk(g_dllBytes.begin() + off,
                                    g_dllBytes.begin() + off + size);
        XorStream(chunk.data(), size, rollingKey);

        if (!SendMsg(s, MsgType::BinaryChunk, chunk.data(), (uint16_t)size))
            return false;

        // Heartbeat point: after every hbInterval chunks, AND after the last chunk
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

            // Extract nonce; malformed HB (no payload) → nonce = zeros → key diverges
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
static void HandleClient(SOCKET csock)
{
    ClientSession s{csock};
    const int64_t now = (int64_t)time(nullptr);

    // ---- 1. Receive Hello ----
    MsgHeader h{};
    if (!RawRecv(s.sock, &h, sizeof(h), 10000)
        || h.Magic   != PROTO_MAGIC
        || h.Version != PROTO_VERSION
        || h.Type    != MsgType::Hello)
    {
        closesocket(csock); return;
    }
    // HelloPayload is fixed-size: reject anything shorter or suspiciously large
    // before allocating or reading further bytes from an untrusted source.
    if (h.Length < sizeof(HelloPayload) || h.Length > sizeof(HelloPayload) + 256)
    {
        closesocket(csock); return;
    }
    std::vector<uint8_t> pay(h.Length);
    if (!RawRecv(s.sock, pay.data(), h.Length)) { closesocket(csock); return; }
    uint32_t crc{}; if (!RawRecv(s.sock, &crc, 4)) { closesocket(csock); return; }

    // Verify Hello CRC (same formula used by SendMsg/RecvMsg)
    {
        uint32_t computed = Crc32((uint8_t*)&h, sizeof(h));
        if (!pay.empty()) computed ^= Crc32(pay.data(), (uint32_t)pay.size());
        if (computed != crc) { closesocket(csock); return; }
    }

    if (pay.size() < sizeof(HelloPayload)) { closesocket(csock); return; }
    const auto& hello = *reinterpret_cast<HelloPayload*>(pay.data());

    s.hwid       = HexStr(hello.HwidHash,   32);
    s.loaderHash = HexStr(hello.LoaderHash, 32);

    std::cout << std::format("[S] Hello HWID={:.16}... build={:08X} loader={:.16}...\n",
                              s.hwid, hello.BuildId, s.loaderHash);

    // ---- 2. Build ID check ----
    if (hello.BuildId != CURRENT_BUILD)
    {
        std::cout << "[S] Reject: stale build\n";
        SendMsg(s, MsgType::AuthFail);
        closesocket(csock); return;
    }

    // ---- 3. DB authorisation (ban + license + integrity check) ----
    {
        std::lock_guard lk(g_dbMu);

        // Always record the HWID + loader hash (even rejected clients)
        g_db.TouchUser(s.hwid, now, s.loaderHash);

        if (!g_db.IsAuthorised(s.hwid, s.loaderHash, now))
        {
            // Check specifically if they're banned so we can give a clear log line
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
            closesocket(csock); return;
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
            closesocket(csock); return;
        }
    }

    // ---- 5. Send AuthOk PLAIN (hdrKey still 0 here) ----
    // CRITICAL: AuthOk MUST be sent before hdrKey is set.
    // The loader reads this raw to extract the token, then derives its own keys.
    // If we encrypt AuthOk with hdrKey, the loader can't decrypt it (chicken-egg).
    {
        AuthOkPayload aok{};
        memcpy(aok.SessionToken, s.token, 16);
        aok.ExpiresAt = (uint32_t)(now + 3600);
        // hdrKey == 0 here → SendMsg sends payload unencrypted
        if (!SendMsg(s, MsgType::AuthOk, &aok, sizeof(aok)))
        { closesocket(csock); return; }
    }

    // ---- 6. NOW derive session keys (all subsequent messages are encrypted) ----
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
        {
            std::cout << "[S] WARNING: config.bin exceeds 65535 bytes — skipping\n";
        }
        else
        {
            SendMsg(s, MsgType::Config, cfg.data(), (uint16_t)cfg.size());
        }
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

    // ---- 9. Post-delivery keep-alive loop ----
    // Client must send a Heartbeat every 25 s (loader interval).
    // We allow up to 2 consecutive misses (covers one transient network blip)
    // before treating the session as dead and evicting it.
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
        hbMisses = 0; // reset on any valid message
        if (mt == MsgType::Heartbeat)
        {
            SendMsg(s, MsgType::Ack);
            std::lock_guard lk(g_dbMu);
            g_db.TouchSession(s.tokenHex, (int64_t)time(nullptr));
        }
    }

    // ---- 10. Cleanup ----
    {
        std::lock_guard lk(g_dbMu);
        g_db.DeleteSession(s.tokenHex);
    }
    --g_activeSessions;
    std::cout << std::format("[S] {:.16}... disconnected (active={})\n",
                              s.hwid, g_activeSessions.load());
    closesocket(csock);
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
            // Raw query since there's no ListHashes() helper — do it inline
            std::cout << "[Admin] (use SQLite browser or add ListHashes() to Database)\n";
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
// Maintenance thread — respects g_running for graceful shutdown.
// Sleeps in 1-second ticks instead of one 5-minute Sleep so shutdown is
// immediate rather than blocking up to 5 minutes.
// ---------------------------------------------------------------------------
static void MaintenanceThread()
{
    int ticks = 0;
    while (g_running.load())
    {
        Sleep(1000);
        if (++ticks < 300) continue; // 300 × 1 s = 5 minutes
        ticks = 0;

        {
            std::lock_guard lk(g_dbMu);
            int n = g_db.PruneSessions((int64_t)time(nullptr), 7200);
            if (n > 0) std::cout << "[S] Pruned " << n << " stale session(s)\n";
        }
        g_rl.Prune(); // free memory from inactive IPs
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // --gen-token: print a fresh 32-byte random hex token and exit
    if (argc >= 2 && std::string(argv[1]) == "--gen-token")
    {
        KeyVault::GenerateToken();
        return 0;
    }

    std::cout << "[SparkyServer] v3.0 (PostgreSQL)\n";

    // Load PostgreSQL connection string
    std::string connstr;
    try { connstr = KeyVault::LoadConnStr(); }
    catch (const std::exception& e)
    {
        std::cerr << "[S] FATAL: " << e.what() << "\n";
        return 1;
    }

    {
        std::lock_guard lk(g_dbMu);
        if (!g_db.Open(connstr))
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
    std::cout << std::format("[S] Listening on :{}\n", LISTEN_PORT);

    while (g_running.load())
    {
        // Poll accept() every 1 second so Ctrl+C / CtrlHandler can break the loop
        // without leaving the listening socket blocked indefinitely.
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

        // Rate limit before spawning a thread — reject at accept() level
        if (!g_rl.Allow(ip))
        {
            // Send nothing — hard-closed socket is the cleanest denial
            closesocket(cs);
            continue;
        }

        std::cout << std::format("[S] Connection from {}\n", ip);
        std::thread(HandleClient, cs).detach();
    }

    std::cout << "[S] Shutting down...\n";
    closesocket(ls);
    {
        std::lock_guard lk(g_dbMu);
        g_db.Close();
    }
    WSACleanup();
    std::cout << "[S] Clean shutdown complete.\n";
    return 0;
}
