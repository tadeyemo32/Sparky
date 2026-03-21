#pragma once
// ---------------------------------------------------------------------------
// Database — PostgreSQL (libpq) persistence layer for SparkyServer
//
// Schema:
//   licenses  (key TEXT PK, tier INT, issued_at INT, expires_at INT,
//              hwid_hash TEXT, note TEXT)
//
//   users     (hwid_hash TEXT PK, license_key TEXT, created_at INT,
//              last_seen INT, is_banned INT, ban_reason TEXT,
//              loader_hash TEXT)
//
//   sessions  (token_hex TEXT PK, hwid_hash TEXT, created_at INT,
//              last_heartbeat INT)
//
//   purchases (id INT PK, hwid_hash TEXT, license_key TEXT,
//              amount_cents INT, purchased_at INT, note TEXT)
//
//   trusted_hashes (hash TEXT PK, note TEXT, added_at INT)
//
// Tier values:
//   1 = Daily   (1 day)
//   2 = Weekly  (7 days)
//   3 = Monthly (30 days)
//   4 = Lifetime (expires_at = 0)
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

// ---------------------------------------------------------------------------
// Row types
// ---------------------------------------------------------------------------
struct LicenseRow
{
    std::string key;          // e.g. "ABCD-EFGH-IJKL-MNOP"
    int         tier;         // 1..4 (Daily/Weekly/Monthly/Lifetime)
    int64_t     issued_at;    // unix timestamp
    int64_t     expires_at;   // 0 = perpetual
    std::string hwid_hash;    // "" = unbound
    std::string note;
};

struct UserRow
{
    std::string hwid_hash;
    std::string license_key;
    int64_t     created_at;
    int64_t     last_seen;
    bool        is_banned    = false;
    std::string ban_reason;
    std::string loader_hash;   // SHA-256 hex of loader binary last seen from this HWID
    std::string username;      // supplied at first login; verified on subsequent logins
    std::string password_hash; // SHA-256 hex of the user's password
    std::string role;          // "user", "admin", or "owner"
};

// Web accounts — completely separate from HWID-based loader users.
// Stored in the web_accounts table.  No HWID, no license required.
struct WebAccountRow
{
    std::string username;
    std::string password_hash; // SHA-256 hex
    std::string role;          // "user", "admin", or "owner"
    int64_t     created_at;
    int64_t     last_login;
    std::string email;
    int         email_verified = 0; // 0 = pending verification, 1 = verified
    std::string otp_code;           // 6-digit OTP (empty when not in use)
    int64_t     otp_expires = 0;    // unix timestamp; 0 = no pending OTP
    int         otp_fail_count = 0; // failed OTP attempts since last new OTP issued
};

// Web sessions — separate from binary-protocol sessions; used by the React site.
struct WebSessionRow
{
    std::string token;      // 64-char hex random token
    std::string username;
    std::string role;       // "user", "admin", or "owner"
    int64_t     created_at;
    int64_t     expires_at;
};

struct SessionRow
{
    std::string token_hex;
    std::string hwid_hash;
    int64_t     created_at;
    int64_t     last_heartbeat;
};

struct PurchaseRow
{
    int64_t     id;
    std::string hwid_hash;
    std::string license_key;
    int32_t     amount_cents;
    int64_t     purchased_at;
    std::string note;
};

struct TrustedHashRow
{
    std::string hash;
    std::string note;
    int64_t     added_at;
};

// ---------------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------------
class Database
{
public:
    // connstr: libpq connection string, e.g.
    //   "host=localhost port=5432 dbname=sparky user=sparky password=s3cr3t sslmode=require"
    // Load via KeyVault::LoadConnStr() at startup.
    bool Open(const std::string& connstr);
    void Close();
    bool IsOpen() const { return m_db != nullptr; }

    // ---------- License management ----------
    bool InsertLicense(const LicenseRow& row);
    std::optional<LicenseRow> GetLicense(const std::string& key) const;
    bool BindLicense(const std::string& key, const std::string& hwid_hash);
    bool RevokeExpiry(const std::string& key); // sets expires_at=1 (epoch) to expire immediately
    // Extends an active license by extra_seconds from its current expires_at.
    // For lifetime licenses (expires_at=0) this sets an explicit future expiry.
    // Returns false if the key doesn't exist.
    bool ExtendLicense(const std::string& key, int64_t extra_seconds);
    // Deletes lingering sessions for users whose license has expired, and returns
    // the count of expired (non-lifetime) licenses found. Call every 12 hours.
    int PruneExpiredLicenses(int64_t now);
    std::vector<LicenseRow> ListLicenses() const;

    // ---------- User management ----------
    // Upsert user + update last_seen. Optionally records loader_hash.
    bool TouchUser(const std::string& hwid_hash, int64_t now,
                   const std::string& loader_hash = "");
    bool SetUserLicense(const std::string& hwid_hash, const std::string& key);
    std::optional<UserRow> GetUser(const std::string& hwid_hash) const;
    std::vector<UserRow> ListUsers() const;
    bool BanUser(const std::string& hwid_hash, const std::string& reason);
    bool UnbanUser(const std::string& hwid_hash);
    // Credential management: on first call stores username+password_hash;
    // on subsequent calls verifies them. Returns 0=ok, 1=auth_failed, -1=db_error.
    int CheckOrStoreCredentials(const std::string& hwid_hash,
                                const std::string& username,
                                const std::string& password_hash_hex);

    // ---------- Session management ----------
    bool InsertSession(const SessionRow& row);
    bool TouchSession(const std::string& token_hex, int64_t now);
    bool DeleteSession(const std::string& token_hex);
    bool DeleteSessionsByHwid(const std::string& hwid_hash);
    std::optional<SessionRow> GetSession(const std::string& token_hex) const;
    int  PruneSessions(int64_t now, int64_t max_age_sec = 7200);

    // ---------- Purchases ----------
    bool InsertPurchase(const PurchaseRow& row);
    std::vector<PurchaseRow> GetPurchases(const std::string& hwid_hash) const;

    // ---------- Trusted loader hashes ----------
    // If the trusted_hashes table is non-empty, only listed hashes pass auth.
    // Empty table = integrity check disabled (dev mode).
    bool AddTrustedHash(const std::string& hash, const std::string& note = "");
    bool RemoveTrustedHash(const std::string& hash);
    bool IsHashTrusted(const std::string& hash) const;
    bool TrustedHashesEnabled() const; // false if table is empty
    std::vector<TrustedHashRow> ListHashes() const;

    // ---------- IP ban list (persistent across restarts) ----------
    // Hard-banned IPs are stored in the DB so they survive server restarts and
    // can be managed via admin.py / admin console without touching iptables.
    bool BanIp(const std::string& ip, const std::string& reason = "rate-limit");
    bool UnbanIp(const std::string& ip);
    bool IsIpBanned(const std::string& ip) const;
    std::vector<std::pair<std::string,std::string>> ListIpBans() const; // {ip, reason}

    // ---------- User role management (web admin panel) ----------
    std::optional<UserRow> GetUserByUsername(const std::string& username) const;
    bool SetUserRole(const std::string& hwid_hash, const std::string& role);
    std::vector<UserRow> ListAdminUsers() const; // role='admin' or 'owner'

    // ---------- Web accounts (React site — no HWID/license required) ----------
    bool CreateWebAccount(const WebAccountRow& row);
    // Upsert owner account: inserts with empty password_hash if new, or updates
    // role/email/email_verified if already exists — never overwrites password_hash.
    bool EnsureOwnerAccount(const std::string& username, const std::string& email);
    std::optional<WebAccountRow> GetWebAccount(const std::string& username) const;
    std::optional<WebAccountRow> GetWebAccountByEmail(const std::string& email) const;
    bool SetWebAccountRole(const std::string& username, const std::string& role);
    bool UpdateWebAccountLastLogin(const std::string& username, int64_t now);
    bool SetWebAccountOtp(const std::string& username, const std::string& otp_code, int64_t otp_expires);
    bool IncrementOtpFailCount(const std::string& username);
    bool VerifyWebAccountEmail(const std::string& username);
    bool UpdateWebAccountPassword(const std::string& username, const std::string& password_hash);
    std::vector<WebAccountRow> ListWebAdmins() const; // role='admin' or 'owner'

    // ---------- Web sessions (React site) ----------
    bool InsertWebSession(const WebSessionRow& row);
    bool DeleteWebSession(const std::string& token);
    std::optional<WebSessionRow> GetWebSession(const std::string& token) const;
    int PruneWebSessions(int64_t now);

    // ---------- Auth helper ----------
    // Full authorisation check: not banned, has valid unexpired license,
    // and (if trusted_hashes is non-empty) loader_hash is in the table.
    bool IsAuthorised(const std::string& hwid_hash,
                      const std::string& loader_hash,
                      int64_t            now) const;

private:
    void* m_db = nullptr; // PGconn*
    bool CreateSchema();
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Human-readable tier name.
inline const char* TierName(int tier)
{
    switch (tier)
    {
        case 1: return "Daily";
        case 2: return "Weekly";
        case 3: return "Monthly";
        case 4: return "Lifetime";
        default: return "Unknown";
    }
}

// Tier → seconds until expiry from issue time (0 = perpetual).
inline int64_t TierLifetimeSeconds(int tier)
{
    switch (tier)
    {
        case 1: return 86400LL;          // 1 day
        case 2: return 86400LL * 7;      // 7 days
        case 3: return 86400LL * 30;     // 30 days
        case 4: return 0;                // Lifetime
        default: return 86400LL;
    }
}
