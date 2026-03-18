#pragma once
// ---------------------------------------------------------------------------
// Database — SQLite3-backed persistence layer for SparkyServer
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
    std::string loader_hash; // SHA-256 hex of loader binary last seen from this HWID
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

// ---------------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------------
class Database
{
public:
    // key: SQLCipher PRAGMA key string in the form "x'<64 hex chars>'"
    //      Pass empty string when building without SPARKY_SQLCIPHER.
    bool Open(const char* path, const std::string& key = "");
    void Close();
    bool IsOpen() const { return m_db != nullptr; }

    // ---------- License management ----------
    bool InsertLicense(const LicenseRow& row);
    std::optional<LicenseRow> GetLicense(const std::string& key) const;
    bool BindLicense(const std::string& key, const std::string& hwid_hash);
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

    // ---------- Session management ----------
    bool InsertSession(const SessionRow& row);
    bool TouchSession(const std::string& token_hex, int64_t now);
    bool DeleteSession(const std::string& token_hex);
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

    // ---------- Auth helper ----------
    // Full authorisation check: not banned, has valid unexpired license,
    // and (if trusted_hashes is non-empty) loader_hash is in the table.
    bool IsAuthorised(const std::string& hwid_hash,
                      const std::string& loader_hash,
                      int64_t            now) const;

private:
    void* m_db = nullptr; // sqlite3*
    bool ExecSQL(const char* sql) const;
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
