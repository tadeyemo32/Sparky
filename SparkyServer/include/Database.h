#pragma once
// ---------------------------------------------------------------------------
// Database — SQLite3-backed persistence layer for SparkyServer
// Schema:
//   licenses  (key TEXT PK, tier INT, issued_at INT, expires_at INT, hwid_hash TEXT)
//   users     (hwid_hash TEXT PK, license_key TEXT, created_at INT, last_seen INT,
//              ban_reason TEXT)
//   sessions  (token_hex TEXT PK, hwid_hash TEXT, created_at INT, last_heartbeat INT)
//   purchases (id INT PK, hwid_hash TEXT, license_key TEXT, amount_cents INT,
//              purchased_at INT, note TEXT)
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

struct LicenseRow
{
    std::string key;         // e.g. "ABCD-EFGH-IJKL-MNOP"
    int         tier;        // 1=basic 2=premium
    int64_t     issued_at;   // unix timestamp
    int64_t     expires_at;  // 0 = never
    std::string hwid_hash;   // "" = unbound
};

struct UserRow
{
    std::string hwid_hash;
    std::string license_key;
    int64_t     created_at;
    int64_t     last_seen;
    std::string ban_reason;  // "" = not banned
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

class Database
{
public:
    // Opens (or creates) the database at `path`.
    // Returns false on failure.
    bool Open(const char* path);
    void Close();
    bool IsOpen() const { return m_db != nullptr; }

    // ---------- License management ----------
    // Insert a freshly generated license. Returns false if key collision.
    bool InsertLicense(const LicenseRow& row);

    // Fetch license by key. nullopt = not found.
    std::optional<LicenseRow> GetLicense(const std::string& key) const;

    // Bind a license to a HWID (first activation). Returns false if already bound.
    bool BindLicense(const std::string& key, const std::string& hwid_hash);

    // List all licenses (admin use).
    std::vector<LicenseRow> ListLicenses() const;

    // ---------- User management ----------
    // Upsert: creates row if hwid unseen, updates last_seen always.
    bool TouchUser(const std::string& hwid_hash, int64_t now);

    // Link user to a license key (after successful bind).
    bool SetUserLicense(const std::string& hwid_hash, const std::string& key);

    // Fetch user by HWID.
    std::optional<UserRow> GetUser(const std::string& hwid_hash) const;

    // Ban / unban.
    bool BanUser(const std::string& hwid_hash, const std::string& reason);
    bool UnbanUser(const std::string& hwid_hash);

    // ---------- Session management ----------
    bool InsertSession(const SessionRow& row);
    bool TouchSession(const std::string& token_hex, int64_t now);
    bool DeleteSession(const std::string& token_hex);
    std::optional<SessionRow> GetSession(const std::string& token_hex) const;

    // Expire sessions older than `max_age_sec` (call periodically).
    int  PruneSessions(int64_t now, int64_t max_age_sec = 7200);

    // ---------- Purchases ----------
    bool InsertPurchase(const PurchaseRow& row);
    std::vector<PurchaseRow> GetPurchases(const std::string& hwid_hash) const;

    // ---------- Auth helpers ----------
    // Returns true if hwid_hash has a valid (non-expired, non-banned) license.
    bool IsAuthorised(const std::string& hwid_hash, int64_t now) const;

private:
    void* m_db = nullptr; // sqlite3*
    bool ExecSQL(const char* sql) const;
    bool CreateSchema();
};
