// Database.cpp — PostgreSQL (libpq) implementation
#include "../include/Database.h"
#include <libpq-fe.h>
#include <ctime>
#include <iostream>

static PGconn* PG(void* p) { return reinterpret_cast<PGconn*>(p); }

// ---------------------------------------------------------------------------
// Internal helper — execute a parameterized query (text protocol).
// All integer parameters must be pre-converted to std::string by callers.
// Returns a valid PGresult* (PGRES_COMMAND_OK or PGRES_TUPLES_OK) that the
// caller MUST PQclear(), or nullptr on failure (already logged).
// ---------------------------------------------------------------------------
static PGresult* PgExec(PGconn* conn, const char* sql,
                         const std::vector<std::string>& args = {})
{
    std::vector<const char*> pv;
    pv.reserve(args.size());
    for (auto& s : args) pv.push_back(s.c_str());

    PGresult* res = PQexecParams(conn, sql,
                                  (int)pv.size(), nullptr,
                                  pv.empty() ? nullptr : pv.data(),
                                  nullptr, nullptr, 0);

    ExecStatusType st = PQresultStatus(res);
    if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK)
    {
        std::cerr << "[DB] " << PQresultErrorMessage(res)
                  << " — query: " << sql << "\n";
        PQclear(res);
        return nullptr;
    }
    return res;
}

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------
bool Database::Open(const std::string& connstr)
{
    PGconn* conn = PQconnectdb(connstr.c_str());
    if (PQstatus(conn) != CONNECTION_OK)
    {
        std::cerr << "[DB] Connection failed: " << PQerrorMessage(conn) << "\n";
        PQfinish(conn);
        return false;
    }
    m_db = conn;
    return CreateSchema();
}

void Database::Close()
{
    if (m_db) { PQfinish(PG(m_db)); m_db = nullptr; }
}

bool Database::CreateSchema()
{
    // PQexec accepts multiple semicolon-separated statements.
    const char* ddl = R"sql(
        CREATE TABLE IF NOT EXISTS licenses (
            key         TEXT PRIMARY KEY,
            tier        INTEGER NOT NULL DEFAULT 1,
            issued_at   BIGINT  NOT NULL,
            expires_at  BIGINT  NOT NULL DEFAULT 0,
            hwid_hash   TEXT    NOT NULL DEFAULT '',
            note        TEXT    NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS users (
            hwid_hash   TEXT PRIMARY KEY,
            license_key TEXT    NOT NULL DEFAULT '',
            created_at  BIGINT  NOT NULL,
            last_seen   BIGINT  NOT NULL,
            is_banned   INTEGER NOT NULL DEFAULT 0,
            ban_reason  TEXT    NOT NULL DEFAULT '',
            loader_hash TEXT    NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS sessions (
            token_hex       TEXT PRIMARY KEY,
            hwid_hash       TEXT   NOT NULL,
            created_at      BIGINT NOT NULL,
            last_heartbeat  BIGINT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS purchases (
            id              BIGSERIAL PRIMARY KEY,
            hwid_hash       TEXT    NOT NULL,
            license_key     TEXT    NOT NULL,
            amount_cents    INTEGER NOT NULL DEFAULT 0,
            purchased_at    BIGINT  NOT NULL,
            note            TEXT    NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS trusted_hashes (
            hash        TEXT PRIMARY KEY,
            note        TEXT   NOT NULL DEFAULT '',
            added_at    BIGINT NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_sessions_hwid  ON sessions(hwid_hash);
        CREATE INDEX IF NOT EXISTS idx_purchases_hwid ON purchases(hwid_hash);
        CREATE INDEX IF NOT EXISTS idx_licenses_hwid  ON licenses(hwid_hash);
    )sql";

    PGresult* res = PQexec(PG(m_db), ddl);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok)
        std::cerr << "[DB] Schema error: " << PQresultErrorMessage(res) << "\n";
    PQclear(res);
    return ok;
}

// ---------------------------------------------------------------------------
// License management
// ---------------------------------------------------------------------------
bool Database::InsertLicense(const LicenseRow& row)
{
    auto* res = PgExec(PG(m_db),
        "INSERT INTO licenses(key,tier,issued_at,expires_at,hwid_hash,note)"
        " VALUES($1,$2,$3,$4,$5,$6)",
        { row.key, std::to_string(row.tier),
          std::to_string(row.issued_at), std::to_string(row.expires_at),
          row.hwid_hash, row.note });
    if (!res) return false;
    PQclear(res);
    return true;
}

std::optional<LicenseRow> Database::GetLicense(const std::string& key) const
{
    auto* res = PgExec(PG(m_db),
        "SELECT key,tier,issued_at,expires_at,hwid_hash,note"
        " FROM licenses WHERE key=$1",
        { key });
    if (!res) return std::nullopt;

    std::optional<LicenseRow> result;
    if (PQntuples(res) == 1)
    {
        LicenseRow r;
        r.key        = PQgetvalue(res, 0, 0);
        r.tier       = std::stoi(PQgetvalue(res, 0, 1));
        r.issued_at  = std::stoll(PQgetvalue(res, 0, 2));
        r.expires_at = std::stoll(PQgetvalue(res, 0, 3));
        r.hwid_hash  = PQgetvalue(res, 0, 4);
        r.note       = PQgetvalue(res, 0, 5);
        result = r;
    }
    PQclear(res);
    return result;
}

bool Database::BindLicense(const std::string& key, const std::string& hwid_hash)
{
    auto* res = PgExec(PG(m_db),
        "UPDATE licenses SET hwid_hash=$1 WHERE key=$2 AND hwid_hash=''",
        { hwid_hash, key });
    if (!res) return false;
    bool changed = std::atoi(PQcmdTuples(res)) > 0;
    PQclear(res);
    return changed;
}

bool Database::RevokeExpiry(const std::string& key)
{
    auto* res = PgExec(PG(m_db),
        "UPDATE licenses SET expires_at=1 WHERE key=$1",
        { key });
    if (!res) return false;
    bool changed = std::atoi(PQcmdTuples(res)) > 0;
    PQclear(res);
    return changed;
}

std::vector<LicenseRow> Database::ListLicenses() const
{
    auto* res = PgExec(PG(m_db),
        "SELECT key,tier,issued_at,expires_at,hwid_hash,note"
        " FROM licenses ORDER BY issued_at DESC");
    std::vector<LicenseRow> rows;
    if (!res) return rows;

    int n = PQntuples(res);
    rows.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        LicenseRow r;
        r.key        = PQgetvalue(res, i, 0);
        r.tier       = std::stoi(PQgetvalue(res, i, 1));
        r.issued_at  = std::stoll(PQgetvalue(res, i, 2));
        r.expires_at = std::stoll(PQgetvalue(res, i, 3));
        r.hwid_hash  = PQgetvalue(res, i, 4);
        r.note       = PQgetvalue(res, i, 5);
        rows.push_back(r);
    }
    PQclear(res);
    return rows;
}

// ---------------------------------------------------------------------------
// User management
// ---------------------------------------------------------------------------
bool Database::TouchUser(const std::string& hwid_hash, int64_t now,
                          const std::string& loader_hash)
{
    PGresult* res;
    if (loader_hash.empty())
    {
        res = PgExec(PG(m_db),
            "INSERT INTO users(hwid_hash,created_at,last_seen)"
            " VALUES($1,$2,$3)"
            " ON CONFLICT(hwid_hash) DO UPDATE SET last_seen=EXCLUDED.last_seen",
            { hwid_hash, std::to_string(now), std::to_string(now) });
    }
    else
    {
        res = PgExec(PG(m_db),
            "INSERT INTO users(hwid_hash,created_at,last_seen,loader_hash)"
            " VALUES($1,$2,$3,$4)"
            " ON CONFLICT(hwid_hash) DO UPDATE"
            " SET last_seen=EXCLUDED.last_seen, loader_hash=EXCLUDED.loader_hash",
            { hwid_hash, std::to_string(now), std::to_string(now), loader_hash });
    }
    if (!res) return false;
    PQclear(res);
    return true;
}

bool Database::SetUserLicense(const std::string& hwid_hash, const std::string& key)
{
    auto* res = PgExec(PG(m_db),
        "UPDATE users SET license_key=$1 WHERE hwid_hash=$2",
        { key, hwid_hash });
    if (!res) return false;
    PQclear(res);
    return true;
}

std::optional<UserRow> Database::GetUser(const std::string& hwid_hash) const
{
    auto* res = PgExec(PG(m_db),
        "SELECT hwid_hash,license_key,created_at,last_seen,is_banned,ban_reason,loader_hash"
        " FROM users WHERE hwid_hash=$1",
        { hwid_hash });
    if (!res) return std::nullopt;

    std::optional<UserRow> result;
    if (PQntuples(res) == 1)
    {
        UserRow r;
        r.hwid_hash   = PQgetvalue(res, 0, 0);
        r.license_key = PQgetvalue(res, 0, 1);
        r.created_at  = std::stoll(PQgetvalue(res, 0, 2));
        r.last_seen   = std::stoll(PQgetvalue(res, 0, 3));
        r.is_banned   = std::atoi(PQgetvalue(res, 0, 4)) != 0;
        r.ban_reason  = PQgetvalue(res, 0, 5);
        r.loader_hash = PQgetvalue(res, 0, 6);
        result = r;
    }
    PQclear(res);
    return result;
}

std::vector<UserRow> Database::ListUsers() const
{
    auto* res = PgExec(PG(m_db),
        "SELECT hwid_hash,license_key,created_at,last_seen,is_banned,ban_reason,loader_hash"
        " FROM users ORDER BY last_seen DESC");
    std::vector<UserRow> rows;
    if (!res) return rows;

    int n = PQntuples(res);
    rows.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        UserRow r;
        r.hwid_hash   = PQgetvalue(res, i, 0);
        r.license_key = PQgetvalue(res, i, 1);
        r.created_at  = std::stoll(PQgetvalue(res, i, 2));
        r.last_seen   = std::stoll(PQgetvalue(res, i, 3));
        r.is_banned   = std::atoi(PQgetvalue(res, i, 4)) != 0;
        r.ban_reason  = PQgetvalue(res, i, 5);
        r.loader_hash = PQgetvalue(res, i, 6);
        rows.push_back(r);
    }
    PQclear(res);
    return rows;
}

bool Database::BanUser(const std::string& hwid_hash, const std::string& reason)
{
    auto* res = PgExec(PG(m_db),
        "UPDATE users SET is_banned=1, ban_reason=$1 WHERE hwid_hash=$2",
        { reason, hwid_hash });
    if (!res) return false;
    PQclear(res);
    return true;
}

bool Database::UnbanUser(const std::string& hwid_hash)
{
    auto* res = PgExec(PG(m_db),
        "UPDATE users SET is_banned=0, ban_reason='' WHERE hwid_hash=$1",
        { hwid_hash });
    if (!res) return false;
    PQclear(res);
    return true;
}

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------
bool Database::InsertSession(const SessionRow& row)
{
    auto* res = PgExec(PG(m_db),
        "INSERT INTO sessions(token_hex,hwid_hash,created_at,last_heartbeat)"
        " VALUES($1,$2,$3,$4)"
        " ON CONFLICT(token_hex) DO UPDATE"
        " SET hwid_hash=EXCLUDED.hwid_hash,"
        "     created_at=EXCLUDED.created_at,"
        "     last_heartbeat=EXCLUDED.last_heartbeat",
        { row.token_hex, row.hwid_hash,
          std::to_string(row.created_at),
          std::to_string(row.last_heartbeat) });
    if (!res) return false;
    PQclear(res);
    return true;
}

bool Database::TouchSession(const std::string& token_hex, int64_t now)
{
    auto* res = PgExec(PG(m_db),
        "UPDATE sessions SET last_heartbeat=$1 WHERE token_hex=$2",
        { std::to_string(now), token_hex });
    if (!res) return false;
    PQclear(res);
    return true;
}

bool Database::DeleteSession(const std::string& token_hex)
{
    auto* res = PgExec(PG(m_db),
        "DELETE FROM sessions WHERE token_hex=$1",
        { token_hex });
    if (!res) return false;
    PQclear(res);
    return true;
}

std::optional<SessionRow> Database::GetSession(const std::string& token_hex) const
{
    auto* res = PgExec(PG(m_db),
        "SELECT token_hex,hwid_hash,created_at,last_heartbeat"
        " FROM sessions WHERE token_hex=$1",
        { token_hex });
    if (!res) return std::nullopt;

    std::optional<SessionRow> result;
    if (PQntuples(res) == 1)
    {
        SessionRow r;
        r.token_hex      = PQgetvalue(res, 0, 0);
        r.hwid_hash      = PQgetvalue(res, 0, 1);
        r.created_at     = std::stoll(PQgetvalue(res, 0, 2));
        r.last_heartbeat = std::stoll(PQgetvalue(res, 0, 3));
        result = r;
    }
    PQclear(res);
    return result;
}

int Database::PruneSessions(int64_t now, int64_t max_age_sec)
{
    auto* res = PgExec(PG(m_db),
        "DELETE FROM sessions WHERE last_heartbeat < $1",
        { std::to_string(now - max_age_sec) });
    if (!res) return 0;
    int n = std::atoi(PQcmdTuples(res));
    PQclear(res);
    return n;
}

// ---------------------------------------------------------------------------
// Purchases
// ---------------------------------------------------------------------------
bool Database::InsertPurchase(const PurchaseRow& row)
{
    auto* res = PgExec(PG(m_db),
        "INSERT INTO purchases(hwid_hash,license_key,amount_cents,purchased_at,note)"
        " VALUES($1,$2,$3,$4,$5)",
        { row.hwid_hash, row.license_key,
          std::to_string(row.amount_cents),
          std::to_string(row.purchased_at),
          row.note });
    if (!res) return false;
    PQclear(res);
    return true;
}

std::vector<PurchaseRow> Database::GetPurchases(const std::string& hwid_hash) const
{
    auto* res = PgExec(PG(m_db),
        "SELECT id,hwid_hash,license_key,amount_cents,purchased_at,note"
        " FROM purchases WHERE hwid_hash=$1 ORDER BY purchased_at DESC",
        { hwid_hash });
    std::vector<PurchaseRow> rows;
    if (!res) return rows;

    int n = PQntuples(res);
    rows.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        PurchaseRow r;
        r.id           = std::stoll(PQgetvalue(res, i, 0));
        r.hwid_hash    = PQgetvalue(res, i, 1);
        r.license_key  = PQgetvalue(res, i, 2);
        r.amount_cents = std::stoi(PQgetvalue(res, i, 3));
        r.purchased_at = std::stoll(PQgetvalue(res, i, 4));
        r.note         = PQgetvalue(res, i, 5);
        rows.push_back(r);
    }
    PQclear(res);
    return rows;
}

// ---------------------------------------------------------------------------
// Trusted loader hashes
// ---------------------------------------------------------------------------
bool Database::AddTrustedHash(const std::string& hash, const std::string& note)
{
    auto* res = PgExec(PG(m_db),
        "INSERT INTO trusted_hashes(hash,note,added_at)"
        " VALUES($1,$2,$3)"
        " ON CONFLICT(hash) DO NOTHING",
        { hash, note, std::to_string((int64_t)time(nullptr)) });
    if (!res) return false;
    PQclear(res);
    return true;
}

bool Database::RemoveTrustedHash(const std::string& hash)
{
    auto* res = PgExec(PG(m_db),
        "DELETE FROM trusted_hashes WHERE hash=$1",
        { hash });
    if (!res) return false;
    PQclear(res);
    return true;
}

bool Database::IsHashTrusted(const std::string& hash) const
{
    auto* res = PgExec(PG(m_db),
        "SELECT 1 FROM trusted_hashes WHERE hash=$1 LIMIT 1",
        { hash });
    if (!res) return false;
    bool found = PQntuples(res) > 0;
    PQclear(res);
    return found;
}

bool Database::TrustedHashesEnabled() const
{
    auto* res = PgExec(PG(m_db), "SELECT COUNT(*) FROM trusted_hashes");
    if (!res) return false;
    bool enabled = std::atoi(PQgetvalue(res, 0, 0)) > 0;
    PQclear(res);
    return enabled;
}

// ---------------------------------------------------------------------------
// Auth helper
// ---------------------------------------------------------------------------
bool Database::IsAuthorised(const std::string& hwid_hash,
                             const std::string& loader_hash,
                             int64_t            now) const
{
    auto user = GetUser(hwid_hash);
    if (!user)                     return false;
    if (user->is_banned)           return false;
    if (user->license_key.empty()) return false;

    auto lic = GetLicense(user->license_key);
    if (!lic)                           return false;
    if (lic->hwid_hash != hwid_hash)    return false;
    if (lic->expires_at != 0 && lic->expires_at < now) return false;

    if (TrustedHashesEnabled() && !loader_hash.empty())
        if (!IsHashTrusted(loader_hash)) return false;

    return true;
}
