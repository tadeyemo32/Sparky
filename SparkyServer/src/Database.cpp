// Database.cpp — SQLite3 implementation of Database
#include "../include/Database.h"
#include "../../deps/sqlite3/sqlite3.h"
#include <cstring>
#include <ctime>
#include <format>
#include <iostream>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static sqlite3* DB(void* p) { return reinterpret_cast<sqlite3*>(p); }

bool Database::ExecSQL(const char* sql) const
{
    char* err = nullptr;
    int rc = sqlite3_exec(DB(m_db), sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK)
    {
        std::cerr << "[DB] SQL error: " << (err ? err : "?") << "\n";
        sqlite3_free(err);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------
bool Database::Open(const char* path)
{
    sqlite3* db = nullptr;
    if (sqlite3_open(path, &db) != SQLITE_OK)
    {
        std::cerr << "[DB] Cannot open: " << path << "\n";
        sqlite3_close(db);
        return false;
    }
    m_db = db;
    sqlite3_exec(DB(m_db), "PRAGMA journal_mode=WAL;",  nullptr, nullptr, nullptr);
    sqlite3_exec(DB(m_db), "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(DB(m_db), "PRAGMA foreign_keys=ON;",   nullptr, nullptr, nullptr);
    return CreateSchema();
}

void Database::Close()
{
    if (m_db) { sqlite3_close(DB(m_db)); m_db = nullptr; }
}

bool Database::CreateSchema()
{
    static const char* ddl = R"sql(
        CREATE TABLE IF NOT EXISTS licenses (
            key         TEXT PRIMARY KEY,
            tier        INTEGER NOT NULL DEFAULT 1,
            issued_at   INTEGER NOT NULL,
            expires_at  INTEGER NOT NULL DEFAULT 0,
            hwid_hash   TEXT    NOT NULL DEFAULT '',
            note        TEXT    NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS users (
            hwid_hash   TEXT PRIMARY KEY,
            license_key TEXT NOT NULL DEFAULT '',
            created_at  INTEGER NOT NULL,
            last_seen   INTEGER NOT NULL,
            ban_reason  TEXT    NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS sessions (
            token_hex       TEXT PRIMARY KEY,
            hwid_hash       TEXT NOT NULL,
            created_at      INTEGER NOT NULL,
            last_heartbeat  INTEGER NOT NULL
        );
        CREATE TABLE IF NOT EXISTS purchases (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            hwid_hash       TEXT    NOT NULL,
            license_key     TEXT    NOT NULL,
            amount_cents    INTEGER NOT NULL DEFAULT 0,
            purchased_at    INTEGER NOT NULL,
            note            TEXT    NOT NULL DEFAULT ''
        );
        CREATE INDEX IF NOT EXISTS idx_sessions_hwid    ON sessions(hwid_hash);
        CREATE INDEX IF NOT EXISTS idx_purchases_hwid   ON purchases(hwid_hash);
        CREATE INDEX IF NOT EXISTS idx_licenses_hwid    ON licenses(hwid_hash);
    )sql";
    return ExecSQL(ddl);
}

// ---------------------------------------------------------------------------
// License management
// ---------------------------------------------------------------------------
bool Database::InsertLicense(const LicenseRow& row)
{
    const char* sql =
        "INSERT INTO licenses(key,tier,issued_at,expires_at,hwid_hash,note)"
        " VALUES(?,?,?,?,?,?);";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text (st, 1, row.key.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (st, 2, row.tier);
    sqlite3_bind_int64(st, 3, row.issued_at);
    sqlite3_bind_int64(st, 4, row.expires_at);
    sqlite3_bind_text (st, 5, row.hwid_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 6, "",                    -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

std::optional<LicenseRow> Database::GetLicense(const std::string& key) const
{
    const char* sql =
        "SELECT key,tier,issued_at,expires_at,hwid_hash FROM licenses WHERE key=?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK)
        return std::nullopt;
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<LicenseRow> result;
    if (sqlite3_step(st) == SQLITE_ROW)
    {
        LicenseRow r;
        r.key       = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        r.tier      = sqlite3_column_int(st, 1);
        r.issued_at = sqlite3_column_int64(st, 2);
        r.expires_at= sqlite3_column_int64(st, 3);
        r.hwid_hash = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
        result = r;
    }
    sqlite3_finalize(st);
    return result;
}

bool Database::BindLicense(const std::string& key, const std::string& hwid_hash)
{
    // Only bind if currently unbound
    const char* sql =
        "UPDATE licenses SET hwid_hash=? WHERE key=? AND hwid_hash='';";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, hwid_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, key.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    bool changed = sqlite3_changes(DB(m_db)) > 0;
    sqlite3_finalize(st);
    return changed;
}

std::vector<LicenseRow> Database::ListLicenses() const
{
    const char* sql =
        "SELECT key,tier,issued_at,expires_at,hwid_hash FROM licenses ORDER BY issued_at DESC;";
    sqlite3_stmt* st = nullptr;
    std::vector<LicenseRow> rows;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK) return rows;
    while (sqlite3_step(st) == SQLITE_ROW)
    {
        LicenseRow r;
        r.key       = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        r.tier      = sqlite3_column_int(st, 1);
        r.issued_at = sqlite3_column_int64(st, 2);
        r.expires_at= sqlite3_column_int64(st, 3);
        r.hwid_hash = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
        rows.push_back(r);
    }
    sqlite3_finalize(st);
    return rows;
}

// ---------------------------------------------------------------------------
// User management
// ---------------------------------------------------------------------------
bool Database::TouchUser(const std::string& hwid_hash, int64_t now)
{
    const char* sql =
        "INSERT INTO users(hwid_hash,created_at,last_seen)"
        " VALUES(?,?,?)"
        " ON CONFLICT(hwid_hash) DO UPDATE SET last_seen=excluded.last_seen;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text (st, 1, hwid_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, now);
    sqlite3_bind_int64(st, 3, now);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool Database::SetUserLicense(const std::string& hwid_hash, const std::string& key)
{
    const char* sql =
        "UPDATE users SET license_key=? WHERE hwid_hash=?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, key.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, hwid_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return true;
}

std::optional<UserRow> Database::GetUser(const std::string& hwid_hash) const
{
    const char* sql =
        "SELECT hwid_hash,license_key,created_at,last_seen,ban_reason"
        " FROM users WHERE hwid_hash=?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK)
        return std::nullopt;
    sqlite3_bind_text(st, 1, hwid_hash.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<UserRow> result;
    if (sqlite3_step(st) == SQLITE_ROW)
    {
        UserRow r;
        r.hwid_hash   = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        r.license_key = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        r.created_at  = sqlite3_column_int64(st, 2);
        r.last_seen   = sqlite3_column_int64(st, 3);
        r.ban_reason  = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
        result = r;
    }
    sqlite3_finalize(st);
    return result;
}

bool Database::BanUser(const std::string& hwid_hash, const std::string& reason)
{
    const char* sql = "UPDATE users SET ban_reason=? WHERE hwid_hash=?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, reason.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, hwid_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return true;
}

bool Database::UnbanUser(const std::string& hwid_hash)
{
    return BanUser(hwid_hash, "");
}

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------
bool Database::InsertSession(const SessionRow& row)
{
    const char* sql =
        "INSERT OR REPLACE INTO sessions(token_hex,hwid_hash,created_at,last_heartbeat)"
        " VALUES(?,?,?,?);";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text (st, 1, row.token_hex.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 2, row.hwid_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, row.created_at);
    sqlite3_bind_int64(st, 4, row.last_heartbeat);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool Database::TouchSession(const std::string& token_hex, int64_t now)
{
    const char* sql =
        "UPDATE sessions SET last_heartbeat=? WHERE token_hex=?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, now);
    sqlite3_bind_text (st, 2, token_hex.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return true;
}

bool Database::DeleteSession(const std::string& token_hex)
{
    const char* sql = "DELETE FROM sessions WHERE token_hex=?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, token_hex.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return true;
}

std::optional<SessionRow> Database::GetSession(const std::string& token_hex) const
{
    const char* sql =
        "SELECT token_hex,hwid_hash,created_at,last_heartbeat"
        " FROM sessions WHERE token_hex=?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK)
        return std::nullopt;
    sqlite3_bind_text(st, 1, token_hex.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<SessionRow> result;
    if (sqlite3_step(st) == SQLITE_ROW)
    {
        SessionRow r;
        r.token_hex      = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        r.hwid_hash      = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        r.created_at     = sqlite3_column_int64(st, 2);
        r.last_heartbeat = sqlite3_column_int64(st, 3);
        result = r;
    }
    sqlite3_finalize(st);
    return result;
}

int Database::PruneSessions(int64_t now, int64_t max_age_sec)
{
    const char* sql = "DELETE FROM sessions WHERE last_heartbeat < ?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, now - max_age_sec);
    sqlite3_step(st);
    int n = sqlite3_changes(DB(m_db));
    sqlite3_finalize(st);
    return n;
}

// ---------------------------------------------------------------------------
// Purchases
// ---------------------------------------------------------------------------
bool Database::InsertPurchase(const PurchaseRow& row)
{
    const char* sql =
        "INSERT INTO purchases(hwid_hash,license_key,amount_cents,purchased_at,note)"
        " VALUES(?,?,?,?,?);";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text (st, 1, row.hwid_hash.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 2, row.license_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (st, 3, row.amount_cents);
    sqlite3_bind_int64(st, 4, row.purchased_at);
    sqlite3_bind_text (st, 5, row.note.c_str(),        -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

std::vector<PurchaseRow> Database::GetPurchases(const std::string& hwid_hash) const
{
    const char* sql =
        "SELECT id,hwid_hash,license_key,amount_cents,purchased_at,note"
        " FROM purchases WHERE hwid_hash=? ORDER BY purchased_at DESC;";
    sqlite3_stmt* st = nullptr;
    std::vector<PurchaseRow> rows;
    if (sqlite3_prepare_v2(DB(m_db), sql, -1, &st, nullptr) != SQLITE_OK) return rows;
    sqlite3_bind_text(st, 1, hwid_hash.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW)
    {
        PurchaseRow r;
        r.id           = sqlite3_column_int64(st, 0);
        r.hwid_hash    = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        r.license_key  = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        r.amount_cents = sqlite3_column_int(st, 3);
        r.purchased_at = sqlite3_column_int64(st, 4);
        r.note         = reinterpret_cast<const char*>(sqlite3_column_text(st, 5));
        rows.push_back(r);
    }
    sqlite3_finalize(st);
    return rows;
}

// ---------------------------------------------------------------------------
// Auth helper
// ---------------------------------------------------------------------------
bool Database::IsAuthorised(const std::string& hwid_hash, int64_t now) const
{
    auto user = GetUser(hwid_hash);
    if (!user) return false;
    if (!user->ban_reason.empty()) return false;
    if (user->license_key.empty()) return false;

    auto lic = GetLicense(user->license_key);
    if (!lic) return false;
    // HWID must match the bound license
    if (lic->hwid_hash != hwid_hash) return false;
    // Check expiry (0 = perpetual)
    if (lic->expires_at != 0 && lic->expires_at < now) return false;
    return true;
}
