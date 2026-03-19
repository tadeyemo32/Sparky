// Database.cpp — PostgreSQL (libpq) implementation
#include "../include/Database.h"
#include <libpq-fe.h>
#include <ctime>
#include <iostream>

static PGconn* PG(void* p) { return reinterpret_cast<PGconn*>(p); }

// ---------------------------------------------------------------------------
// NULL-safe column accessors.
// PQgetvalue() returns "" for NULL columns; stoi/stoll would throw on "".
// These helpers return a safe default (0 / "") instead.
// ---------------------------------------------------------------------------
static int PgInt(PGresult* r, int row, int col)
{
    if (PQgetisnull(r, row, col)) return 0;
    const char* v = PQgetvalue(r, row, col);
    if (!v || !*v) return 0;
    try { return std::stoi(v); } catch (...) { return 0; }
}

static int64_t PgInt64(PGresult* r, int row, int col)
{
    if (PQgetisnull(r, row, col)) return 0;
    const char* v = PQgetvalue(r, row, col);
    if (!v || !*v) return 0;
    try { return std::stoll(v); } catch (...) { return 0; }
}

// ---------------------------------------------------------------------------
// Internal helper — execute a parameterized query (text protocol).
// All integer parameters must be pre-converted to std::string by callers.
// Returns a valid PGresult* (PGRES_COMMAND_OK or PGRES_TUPLES_OK) that the
// caller MUST PQclear(), or nullptr on failure (already logged).
//
// If the connection is found to be broken after a failure, PQreset() is
// attempted once and the query retried — this makes the server resilient to
// transient DB restarts without needing a full process restart.
// ---------------------------------------------------------------------------
static PGresult* PgExec(PGconn* conn, const char* sql,
                         const std::vector<std::string>& args = {})
{
    std::vector<const char*> pv;
    pv.reserve(args.size());
    for (auto& s : args) pv.push_back(s.c_str());

    auto exec = [&]() -> PGresult* {
        return PQexecParams(conn, sql,
                            (int)pv.size(), nullptr,
                            pv.empty() ? nullptr : pv.data(),
                            nullptr, nullptr, 0);
    };

    PGresult* res = exec();
    ExecStatusType st = PQresultStatus(res);

    if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK)
    {
        std::cerr << "[DB] " << PQresultErrorMessage(res)
                  << " — query: " << sql << "\n";
        PQclear(res);

        // If the connection is broken, attempt a single reconnect + retry.
        if (PQstatus(conn) == CONNECTION_BAD)
        {
            std::cerr << "[DB] Connection lost — attempting PQreset()...\n";
            PQreset(conn);
            if (PQstatus(conn) == CONNECTION_OK)
            {
                std::cerr << "[DB] Reconnected — retrying query...\n";
                res = exec();
                st  = PQresultStatus(res);
                if (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK)
                    return res;
                std::cerr << "[DB] Retry failed: " << PQresultErrorMessage(res) << "\n";
                PQclear(res);
            }
            else
            {
                std::cerr << "[DB] PQreset() failed — DB still unreachable\n";
            }
        }
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
        CREATE TABLE IF NOT EXISTS admins (
            username      TEXT PRIMARY KEY,
            email         TEXT NOT NULL,
            password_hash TEXT NOT NULL,
            role          TEXT NOT NULL DEFAULT 'admin',
            created_at    BIGINT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS ip_bans (
            ip          TEXT PRIMARY KEY,
            reason      TEXT   NOT NULL DEFAULT '',
            banned_at   BIGINT NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_sessions_hwid  ON sessions(hwid_hash);
        CREATE INDEX IF NOT EXISTS idx_purchases_hwid ON purchases(hwid_hash);
        CREATE INDEX IF NOT EXISTS idx_licenses_hwid  ON licenses(hwid_hash);
        ALTER TABLE users ADD COLUMN IF NOT EXISTS username      TEXT NOT NULL DEFAULT '';
        ALTER TABLE users ADD COLUMN IF NOT EXISTS password_hash TEXT NOT NULL DEFAULT '';
        ALTER TABLE users ADD COLUMN IF NOT EXISTS role          TEXT NOT NULL DEFAULT 'user';
        CREATE TABLE IF NOT EXISTS web_sessions (
            token      TEXT PRIMARY KEY,
            username   TEXT   NOT NULL,
            role       TEXT   NOT NULL DEFAULT 'user',
            created_at BIGINT NOT NULL,
            expires_at BIGINT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_web_sessions_user ON web_sessions(username);
        CREATE TABLE IF NOT EXISTS web_accounts (
            username      TEXT PRIMARY KEY,
            password_hash TEXT   NOT NULL,
            role          TEXT   NOT NULL DEFAULT 'user',
            created_at    BIGINT NOT NULL,
            last_login    BIGINT NOT NULL DEFAULT 0
        );
        ALTER TABLE web_accounts ADD COLUMN IF NOT EXISTS email          TEXT    NOT NULL DEFAULT '';
        ALTER TABLE web_accounts ADD COLUMN IF NOT EXISTS email_verified INTEGER NOT NULL DEFAULT 0;
        ALTER TABLE web_accounts ADD COLUMN IF NOT EXISTS otp_code       TEXT    NOT NULL DEFAULT '';
        ALTER TABLE web_accounts ADD COLUMN IF NOT EXISTS otp_expires    BIGINT  NOT NULL DEFAULT 0;
        CREATE INDEX IF NOT EXISTS idx_web_accounts_email ON web_accounts(email);
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
        r.tier       = PgInt  (res, 0, 1);
        r.issued_at  = PgInt64(res, 0, 2);
        r.expires_at = PgInt64(res, 0, 3);
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

bool Database::ExtendLicense(const std::string& key, int64_t extra_seconds)
{
    // Fetch current expires_at so we can add to it.
    auto lic = GetLicense(key);
    if (!lic) return false;

    int64_t base = lic->expires_at;
    if (base == 0)
    {
        // Lifetime license: treat current time as the base so the extension
        // creates a meaningful future expiry rather than 1970 + extra_seconds.
        base = (int64_t)time(nullptr);
    }
    else if (base < (int64_t)time(nullptr))
    {
        // Already expired: extend from now, not from the past expiry.
        base = (int64_t)time(nullptr);
    }

    const int64_t new_expiry = base + extra_seconds;
    auto* res = PgExec(PG(m_db),
        "UPDATE licenses SET expires_at=$1 WHERE key=$2",
        { std::to_string(new_expiry), key });
    if (!res) return false;
    bool changed = std::atoi(PQcmdTuples(res)) > 0;
    PQclear(res);
    return changed;
}

int Database::PruneExpiredLicenses(int64_t now)
{
    // Remove sessions whose owner's license has expired so those connections
    // are dropped at the next heartbeat / reconnect attempt.
    // IsAuthorised already blocks re-auth, but this cleans up lingering rows.
    auto* res = PgExec(PG(m_db),
        "DELETE FROM sessions WHERE hwid_hash IN ("
        "  SELECT u.hwid_hash FROM users u"
        "  JOIN licenses l ON l.key = u.license_key"
        "  WHERE l.expires_at != 0 AND l.expires_at < $1"
        ")",
        { std::to_string(now) });
    if (res) PQclear(res);

    // Return count of expired (non-lifetime) licenses for logging.
    auto* cnt = PgExec(PG(m_db),
        "SELECT COUNT(*) FROM licenses WHERE expires_at != 0 AND expires_at < $1",
        { std::to_string(now) });
    int expired = 0;
    if (cnt) { expired = std::atoi(PQgetvalue(cnt, 0, 0)); PQclear(cnt); }
    return expired;
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
        r.tier       = PgInt  (res, i, 1);
        r.issued_at  = PgInt64(res, i, 2);
        r.expires_at = PgInt64(res, i, 3);
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
        "SELECT hwid_hash,license_key,created_at,last_seen,is_banned,ban_reason,"
        "       loader_hash,username,password_hash,role"
        " FROM users WHERE hwid_hash=$1",
        { hwid_hash });
    if (!res) return std::nullopt;

    std::optional<UserRow> result;
    if (PQntuples(res) == 1)
    {
        UserRow r;
        r.hwid_hash     = PQgetvalue(res, 0, 0);
        r.license_key   = PQgetvalue(res, 0, 1);
        r.created_at    = PgInt64(res, 0, 2);
        r.last_seen     = PgInt64(res, 0, 3);
        r.is_banned     = PgInt  (res, 0, 4) != 0;
        r.ban_reason    = PQgetvalue(res, 0, 5);
        r.loader_hash   = PQgetvalue(res, 0, 6);
        r.username      = PQgetvalue(res, 0, 7);
        r.password_hash = PQgetvalue(res, 0, 8);
        r.role          = PQgetvalue(res, 0, 9);
        if (r.role.empty()) r.role = "user";
        result = r;
    }
    PQclear(res);
    return result;
}

std::optional<UserRow> Database::GetUserByUsername(const std::string& username) const
{
    auto* res = PgExec(PG(m_db),
        "SELECT hwid_hash,license_key,created_at,last_seen,is_banned,ban_reason,"
        "       loader_hash,username,password_hash,role"
        " FROM users WHERE username=$1 LIMIT 1",
        { username });
    if (!res) return std::nullopt;

    std::optional<UserRow> result;
    if (PQntuples(res) == 1)
    {
        UserRow r;
        r.hwid_hash     = PQgetvalue(res, 0, 0);
        r.license_key   = PQgetvalue(res, 0, 1);
        r.created_at    = PgInt64(res, 0, 2);
        r.last_seen     = PgInt64(res, 0, 3);
        r.is_banned     = PgInt  (res, 0, 4) != 0;
        r.ban_reason    = PQgetvalue(res, 0, 5);
        r.loader_hash   = PQgetvalue(res, 0, 6);
        r.username      = PQgetvalue(res, 0, 7);
        r.password_hash = PQgetvalue(res, 0, 8);
        r.role          = PQgetvalue(res, 0, 9);
        if (r.role.empty()) r.role = "user";
        result = r;
    }
    PQclear(res);
    return result;
}

bool Database::SetUserRole(const std::string& hwid_hash, const std::string& role)
{
    auto* res = PgExec(PG(m_db),
        "UPDATE users SET role=$1 WHERE hwid_hash=$2",
        { role, hwid_hash });
    if (!res) return false;
    PQclear(res);
    return true;
}

std::vector<UserRow> Database::ListAdminUsers() const
{
    auto* res = PgExec(PG(m_db),
        "SELECT hwid_hash,license_key,created_at,last_seen,is_banned,ban_reason,"
        "       loader_hash,username,password_hash,role"
        " FROM users WHERE role IN ('admin','owner') ORDER BY last_seen DESC");
    std::vector<UserRow> rows;
    if (!res) return rows;
    int n = PQntuples(res);
    rows.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        UserRow r;
        r.hwid_hash     = PQgetvalue(res, i, 0);
        r.license_key   = PQgetvalue(res, i, 1);
        r.created_at    = PgInt64(res, i, 2);
        r.last_seen     = PgInt64(res, i, 3);
        r.is_banned     = PgInt  (res, i, 4) != 0;
        r.ban_reason    = PQgetvalue(res, i, 5);
        r.loader_hash   = PQgetvalue(res, i, 6);
        r.username      = PQgetvalue(res, i, 7);
        r.password_hash = PQgetvalue(res, i, 8);
        r.role          = PQgetvalue(res, i, 9);
        if (r.role.empty()) r.role = "user";
        rows.push_back(r);
    }
    PQclear(res);
    return rows;
}

std::vector<UserRow> Database::ListUsers() const
{
    auto* res = PgExec(PG(m_db),
        "SELECT hwid_hash,license_key,created_at,last_seen,is_banned,ban_reason,"
        "       loader_hash,username,password_hash,role"
        " FROM users ORDER BY last_seen DESC");
    std::vector<UserRow> rows;
    if (!res) return rows;

    int n = PQntuples(res);
    rows.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        UserRow r;
        r.hwid_hash     = PQgetvalue(res, i, 0);
        r.license_key   = PQgetvalue(res, i, 1);
        r.created_at    = PgInt64(res, i, 2);
        r.last_seen     = PgInt64(res, i, 3);
        r.is_banned     = PgInt  (res, i, 4) != 0;
        r.ban_reason    = PQgetvalue(res, i, 5);
        r.loader_hash   = PQgetvalue(res, i, 6);
        r.username      = PQgetvalue(res, i, 7);
        r.password_hash = PQgetvalue(res, i, 8);
        r.role          = PQgetvalue(res, i, 9);
        if (r.role.empty()) r.role = "user";
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

int Database::CheckOrStoreCredentials(const std::string& hwid_hash,
                                       const std::string& username_in,
                                       const std::string& password_hash_in)
{
    auto user = GetUser(hwid_hash);
    if (!user) return -1;

    if (user->username.empty() && user->password_hash.empty())
    {
        // First login from this hardware: store the credentials.
        auto* res = PgExec(PG(m_db),
            "UPDATE users SET username=$1, password_hash=$2 WHERE hwid_hash=$3",
            { username_in, password_hash_in, hwid_hash });
        if (!res) return -1;
        PQclear(res);
        return 0;
    }

    // Verify submitted credentials match what is stored.
    if (user->username != username_in || user->password_hash != password_hash_in)
        return 1; // auth failed

    return 0;
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
        r.created_at     = PgInt64(res, 0, 2);
        r.last_heartbeat = PgInt64(res, 0, 3);
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
        r.id           = PgInt64(res, i, 0);
        r.hwid_hash    = PQgetvalue(res, i, 1);
        r.license_key  = PQgetvalue(res, i, 2);
        r.amount_cents = PgInt  (res, i, 3);
        r.purchased_at = PgInt64(res, i, 4);
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

std::vector<TrustedHashRow> Database::ListHashes() const
{
    auto* res = PgExec(PG(m_db),
        "SELECT hash,note,added_at FROM trusted_hashes ORDER BY added_at DESC");
    std::vector<TrustedHashRow> rows;
    if (!res) return rows;

    int n = PQntuples(res);
    rows.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        TrustedHashRow r;
        r.hash     = PQgetvalue(res, i, 0);
        r.note     = PQgetvalue(res, i, 1);
        r.added_at = PgInt64(res, i, 2);
        rows.push_back(r);
    }
    PQclear(res);
    return rows;
}

// ---------------------------------------------------------------------------
// IP ban list — persistent across restarts
// ---------------------------------------------------------------------------
bool Database::BanIp(const std::string& ip, const std::string& reason)
{
    auto* res = PgExec(PG(m_db),
        "INSERT INTO ip_bans(ip,reason,banned_at) VALUES($1,$2,$3)"
        " ON CONFLICT(ip) DO UPDATE SET reason=EXCLUDED.reason, banned_at=EXCLUDED.banned_at",
        { ip, reason, std::to_string((int64_t)time(nullptr)) });
    if (!res) return false;
    PQclear(res);
    return true;
}

bool Database::UnbanIp(const std::string& ip)
{
    auto* res = PgExec(PG(m_db), "DELETE FROM ip_bans WHERE ip=$1", { ip });
    if (!res) return false;
    PQclear(res);
    return true;
}

bool Database::IsIpBanned(const std::string& ip) const
{
    auto* res = PgExec(PG(m_db), "SELECT 1 FROM ip_bans WHERE ip=$1 LIMIT 1", { ip });
    if (!res) return false;
    bool found = PQntuples(res) > 0;
    PQclear(res);
    return found;
}

std::vector<std::pair<std::string,std::string>> Database::ListIpBans() const
{
    auto* res = PgExec(PG(m_db),
        "SELECT ip, reason FROM ip_bans ORDER BY banned_at DESC");
    std::vector<std::pair<std::string,std::string>> rows;
    if (!res) return rows;
    int n = PQntuples(res);
    rows.reserve(n);
    for (int i = 0; i < n; ++i)
        rows.emplace_back(PQgetvalue(res, i, 0), PQgetvalue(res, i, 1));
    PQclear(res);
    return rows;
}

// ---------------------------------------------------------------------------
// Web accounts (React site — username+password only, no HWID/license)
// ---------------------------------------------------------------------------
bool Database::CreateWebAccount(const WebAccountRow& row)
{
    auto* res = PgExec(PG(m_db),
        "INSERT INTO web_accounts(username,password_hash,role,created_at,last_login,email,email_verified)"
        " VALUES($1,$2,$3,$4,$5,$6,$7)"
        " ON CONFLICT(username) DO NOTHING",
        { row.username, row.password_hash, row.role,
          std::to_string(row.created_at), std::to_string(row.last_login),
          row.email, std::to_string(row.email_verified) });
    if (!res) return false;
    bool created = std::atoi(PQcmdTuples(res)) > 0;
    PQclear(res);
    return created;
}

std::optional<WebAccountRow> Database::GetWebAccount(const std::string& username) const
{
    auto* res = PgExec(PG(m_db),
        "SELECT username,password_hash,role,created_at,last_login,email,email_verified,otp_code,otp_expires"
        " FROM web_accounts WHERE username=$1",
        { username });
    if (!res) return std::nullopt;

    std::optional<WebAccountRow> result;
    if (PQntuples(res) == 1)
    {
        WebAccountRow r;
        r.username       = PQgetvalue(res, 0, 0);
        r.password_hash  = PQgetvalue(res, 0, 1);
        r.role           = PQgetvalue(res, 0, 2);
        r.created_at     = PgInt64(res, 0, 3);
        r.last_login     = PgInt64(res, 0, 4);
        r.email          = PQgetvalue(res, 0, 5);
        r.email_verified = PgInt(res, 0, 6);
        r.otp_code       = PQgetvalue(res, 0, 7);
        r.otp_expires    = PgInt64(res, 0, 8);
        if (r.role.empty()) r.role = "user";
        result = r;
    }
    PQclear(res);
    return result;
}

bool Database::SetWebAccountRole(const std::string& username, const std::string& role)
{
    auto* res = PgExec(PG(m_db),
        "UPDATE web_accounts SET role=$1 WHERE username=$2",
        { role, username });
    if (!res) return false;
    PQclear(res);
    return true;
}

bool Database::UpdateWebAccountLastLogin(const std::string& username, int64_t now)
{
    auto* res = PgExec(PG(m_db),
        "UPDATE web_accounts SET last_login=$1 WHERE username=$2",
        { std::to_string(now), username });
    if (!res) return false;
    PQclear(res);
    return true;
}

std::vector<WebAccountRow> Database::ListWebAdmins() const
{
    auto* res = PgExec(PG(m_db),
        "SELECT username,password_hash,role,created_at,last_login"
        " FROM web_accounts WHERE role IN ('admin','owner') ORDER BY username");
    std::vector<WebAccountRow> rows;
    if (!res) return rows;
    int n = PQntuples(res);
    rows.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        WebAccountRow r;
        r.username      = PQgetvalue(res, i, 0);
        r.password_hash = PQgetvalue(res, i, 1);
        r.role          = PQgetvalue(res, i, 2);
        r.created_at    = PgInt64(res, i, 3);
        r.last_login    = PgInt64(res, i, 4);
        if (r.role.empty()) r.role = "user";
        rows.push_back(r);
    }
    PQclear(res);
    return rows;
}

std::optional<WebAccountRow> Database::GetWebAccountByEmail(const std::string& email) const
{
    if (email.empty()) return std::nullopt;
    auto* res = PgExec(PG(m_db),
        "SELECT username,password_hash,role,created_at,last_login,email,email_verified,otp_code,otp_expires"
        " FROM web_accounts WHERE email=$1 LIMIT 1",
        { email });
    if (!res) return std::nullopt;

    std::optional<WebAccountRow> result;
    if (PQntuples(res) == 1)
    {
        WebAccountRow r;
        r.username       = PQgetvalue(res, 0, 0);
        r.password_hash  = PQgetvalue(res, 0, 1);
        r.role           = PQgetvalue(res, 0, 2);
        r.created_at     = PgInt64(res, 0, 3);
        r.last_login     = PgInt64(res, 0, 4);
        r.email          = PQgetvalue(res, 0, 5);
        r.email_verified = PgInt(res, 0, 6);
        r.otp_code       = PQgetvalue(res, 0, 7);
        r.otp_expires    = PgInt64(res, 0, 8);
        if (r.role.empty()) r.role = "user";
        result = r;
    }
    PQclear(res);
    return result;
}

bool Database::SetWebAccountOtp(const std::string& username,
                                 const std::string& otp_code,
                                 int64_t            otp_expires)
{
    auto* res = PgExec(PG(m_db),
        "UPDATE web_accounts SET otp_code=$1,otp_expires=$2 WHERE username=$3",
        { otp_code, std::to_string(otp_expires), username });
    if (!res) return false;
    PQclear(res);
    return true;
}

bool Database::VerifyWebAccountEmail(const std::string& username)
{
    auto* res = PgExec(PG(m_db),
        "UPDATE web_accounts SET email_verified=1,otp_code='',otp_expires=0 WHERE username=$1",
        { username });
    if (!res) return false;
    PQclear(res);
    return true;
}

bool Database::UpdateWebAccountPassword(const std::string& username,
                                         const std::string& password_hash)
{
    auto* res = PgExec(PG(m_db),
        "UPDATE web_accounts SET password_hash=$1,otp_code='',otp_expires=0 WHERE username=$2",
        { password_hash, username });
    if (!res) return false;
    PQclear(res);
    return true;
}

// ---------------------------------------------------------------------------
// Web sessions (React site)
// ---------------------------------------------------------------------------
bool Database::InsertWebSession(const WebSessionRow& row)
{
    auto* res = PgExec(PG(m_db),
        "INSERT INTO web_sessions(token,username,role,created_at,expires_at)"
        " VALUES($1,$2,$3,$4,$5)"
        " ON CONFLICT(token) DO UPDATE"
        " SET username=EXCLUDED.username, role=EXCLUDED.role,"
        "     created_at=EXCLUDED.created_at, expires_at=EXCLUDED.expires_at",
        { row.token, row.username, row.role,
          std::to_string(row.created_at), std::to_string(row.expires_at) });
    if (!res) return false;
    PQclear(res);
    return true;
}

bool Database::DeleteWebSession(const std::string& token)
{
    auto* res = PgExec(PG(m_db),
        "DELETE FROM web_sessions WHERE token=$1", { token });
    if (!res) return false;
    PQclear(res);
    return true;
}

std::optional<WebSessionRow> Database::GetWebSession(const std::string& token) const
{
    auto* res = PgExec(PG(m_db),
        "SELECT token,username,role,created_at,expires_at"
        " FROM web_sessions WHERE token=$1",
        { token });
    if (!res) return std::nullopt;

    std::optional<WebSessionRow> result;
    if (PQntuples(res) == 1)
    {
        WebSessionRow r;
        r.token      = PQgetvalue(res, 0, 0);
        r.username   = PQgetvalue(res, 0, 1);
        r.role       = PQgetvalue(res, 0, 2);
        r.created_at = PgInt64(res, 0, 3);
        r.expires_at = PgInt64(res, 0, 4);
        result = r;
    }
    PQclear(res);
    return result;
}

int Database::PruneWebSessions(int64_t now)
{
    auto* res = PgExec(PG(m_db),
        "DELETE FROM web_sessions WHERE expires_at < $1",
        { std::to_string(now) });
    if (!res) return 0;
    int n = std::atoi(PQcmdTuples(res));
    PQclear(res);
    return n;
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
