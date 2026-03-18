#!/usr/bin/env python3
"""
sparky_admin.py — Python-side admin tool for SparkyServer.

Uses the same SQLite/SQLCipher database file that the C++ server writes to.
Can run while the server is live (WAL mode makes concurrent reads safe).

SQLCipher support:
  pip install sqlcipher3          # needs libsqlcipher-dev on Linux
  Set SPARKY_DB_KEY=<64 hex chars> before running (same key as C++ server)

Usage:
    python admin.py <db_path> <command> [args...]

Examples:
    python admin.py sparky.db status
    python admin.py sparky.db gen-key             # generate a fresh 32-byte key
    python admin.py sparky.db issue weekly
    python admin.py sparky.db issue daily --days 1
    python admin.py sparky.db issue lifetime
    python admin.py sparky.db ban  <hwid_hex> "caught cheating"
    python admin.py sparky.db unban <hwid_hex>
    python admin.py sparky.db activate <KEY> <hwid_hex>
    python admin.py sparky.db list-licenses
    python admin.py sparky.db list-users
    python admin.py sparky.db purchases <hwid_hex>
    python admin.py sparky.db add-hash <sha256_hex> [note]
    python admin.py sparky.db rm-hash <sha256_hex>
    python admin.py sparky.db list-hashes
    python admin.py sparky.db prune
    python admin.py sparky.db watch            # live-tail new connections
"""

import sys
import os
import time
import random
import sqlite3
import argparse
from datetime import datetime, timezone

# ---------------------------------------------------------------------------
# SQLCipher: try pysqlcipher3 first; fall back to plain sqlite3 with warning.
# ---------------------------------------------------------------------------
_SQLCIPHER = False
try:
    from sqlcipher3 import dbapi2 as _sql_module
    _SQLCIPHER = True
except ImportError:
    _sql_module = sqlite3

DB_PATH = "sparky.db"

# ---------------------------------------------------------------------------
# Key generation (mirrors C++ LicenseManager)
# ---------------------------------------------------------------------------
KEY_CHARSET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"  # no O/I/0/1

def generate_key() -> str:
    """Generate a XXXX-XXXX-XXXX-XXXX license key (80-bit entropy)."""
    raw = random.randbytes(10)
    # Pack 80 bits into a single integer
    value = int.from_bytes(raw, "big")
    chars = []
    for _ in range(16):
        chars.append(KEY_CHARSET[value & 0x1F])
        value >>= 5
    chars.reverse()
    return "-".join("".join(chars[i:i+4]) for i in range(0, 16, 4))


# ---------------------------------------------------------------------------
# Tier helpers
# ---------------------------------------------------------------------------
TIER_NAMES    = {1: "Daily", 2: "Weekly", 3: "Monthly", 4: "Lifetime"}
TIER_DAYS     = {1: 1,       2: 7,        3: 30,        4: 0}
TIER_BY_NAME  = {v.lower(): k for k, v in TIER_NAMES.items()}


def tier_id(name_or_int: str) -> int:
    try:
        return int(name_or_int)
    except ValueError:
        t = TIER_BY_NAME.get(name_or_int.lower())
        if t is None:
            raise ValueError(f"Unknown tier '{name_or_int}'. Use: daily weekly monthly lifetime")
        return t


def expires_at(tier: int, days_override: int = 0, now: int = 0) -> int:
    now = now or int(time.time())
    days = days_override or TIER_DAYS[tier]
    if days == 0:
        return 0  # perpetual
    return now + days * 86400


# ---------------------------------------------------------------------------
# Key loading (mirrors KeyVault.h resolution order)
# ---------------------------------------------------------------------------
def _load_key() -> str | None:
    """Return SQLCipher key string  "x'<64hex>'"  or None if not configured."""
    hex_key = None
    if k := os.environ.get("SPARKY_DB_KEY"):
        hex_key = k.strip()
    elif kf := os.environ.get("SPARKY_DB_KEYFILE"):
        with open(kf) as f:
            hex_key = f.readline().strip()
    elif os.path.exists("sparky.key"):
        with open("sparky.key") as f:
            hex_key = f.readline().strip()

    if hex_key is None:
        return None
    if len(hex_key) != 64 or not all(c in "0123456789abcdefABCDEF" for c in hex_key):
        raise ValueError("SPARKY_DB_KEY must be exactly 64 hex characters")
    return f"x'{hex_key.lower()}'"


# ---------------------------------------------------------------------------
# DB helpers
# ---------------------------------------------------------------------------
def connect(db_path: str) -> _sql_module.Connection:
    con = _sql_module.connect(db_path, timeout=10.0)

    if _SQLCIPHER:
        key = _load_key()
        if key is None:
            print("WARNING: SQLCipher available but no key set (SPARKY_DB_KEY)."
                  " Trying to open as plaintext — this will fail if DB is encrypted.",
                  file=sys.stderr)
        else:
            con.execute(f'PRAGMA key = "{key}"')
            con.execute("PRAGMA cipher_page_size = 65536")
            con.execute("PRAGMA kdf_iter = 256000")
            con.execute("PRAGMA cipher_hmac_algorithm = HMAC_SHA512")
            con.execute("PRAGMA cipher_kdf_algorithm = PBKDF2_HMAC_SHA512")
    else:
        if os.environ.get("SPARKY_DB_KEY"):
            print("WARNING: SPARKY_DB_KEY is set but sqlcipher3 is not installed."
                  " Run:  pip install sqlcipher3", file=sys.stderr)

    con.execute("PRAGMA journal_mode=WAL")
    con.execute("PRAGMA foreign_keys=ON")
    con.row_factory = _sql_module.Row
    return con


def ensure_schema(con: sqlite3.Connection):
    con.executescript("""
        CREATE TABLE IF NOT EXISTS licenses (
            key TEXT PRIMARY KEY, tier INTEGER NOT NULL DEFAULT 1,
            issued_at INTEGER NOT NULL, expires_at INTEGER NOT NULL DEFAULT 0,
            hwid_hash TEXT NOT NULL DEFAULT '', note TEXT NOT NULL DEFAULT '');
        CREATE TABLE IF NOT EXISTS users (
            hwid_hash TEXT PRIMARY KEY, license_key TEXT NOT NULL DEFAULT '',
            created_at INTEGER NOT NULL, last_seen INTEGER NOT NULL,
            is_banned INTEGER NOT NULL DEFAULT 0,
            ban_reason TEXT NOT NULL DEFAULT '',
            loader_hash TEXT NOT NULL DEFAULT '');
        CREATE TABLE IF NOT EXISTS sessions (
            token_hex TEXT PRIMARY KEY, hwid_hash TEXT NOT NULL,
            created_at INTEGER NOT NULL, last_heartbeat INTEGER NOT NULL);
        CREATE TABLE IF NOT EXISTS purchases (
            id INTEGER PRIMARY KEY AUTOINCREMENT, hwid_hash TEXT NOT NULL,
            license_key TEXT NOT NULL, amount_cents INTEGER NOT NULL DEFAULT 0,
            purchased_at INTEGER NOT NULL, note TEXT NOT NULL DEFAULT '');
        CREATE TABLE IF NOT EXISTS trusted_hashes (
            hash TEXT PRIMARY KEY, note TEXT NOT NULL DEFAULT '',
            added_at INTEGER NOT NULL DEFAULT 0);
    """)
    con.commit()


def fmt_ts(ts: int) -> str:
    if ts == 0:
        return "never"
    return datetime.fromtimestamp(ts, tz=timezone.utc).strftime("%Y-%m-%d %H:%M UTC")


def fmt_hwid(h: str) -> str:
    return h[:16] + "..." if len(h) > 16 else h


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------
def cmd_gen_key(_con, _args):
    """Print a cryptographically random 32-byte hex key."""
    key = os.urandom(32).hex()
    print(f"\n  {key}\n")
    print("  Save this as SPARKY_DB_KEY in your environment,")
    print("  or write it to sparky.key (chmod 400, owned by server user).")
    print("  Keep it secret — if lost, the database cannot be opened.\n")


def cmd_status(con, _args):
    r = con.execute("""
        SELECT
            (SELECT COUNT(*) FROM licenses)                        AS total_keys,
            (SELECT COUNT(*) FROM licenses WHERE hwid_hash != '')  AS bound_keys,
            (SELECT COUNT(*) FROM users)                           AS total_users,
            (SELECT COUNT(*) FROM users WHERE is_banned = 1)       AS banned,
            (SELECT COUNT(*) FROM sessions)                        AS sessions,
            (SELECT COUNT(*) FROM trusted_hashes)                  AS hashes
    """).fetchone()
    now = int(time.time())
    expired = con.execute(
        "SELECT COUNT(*) FROM licenses WHERE expires_at > 0 AND expires_at < ?", (now,)
    ).fetchone()[0]
    print(f"""
Server database status
  Licenses:        {r['total_keys']} total, {r['bound_keys']} bound, {expired} expired
  Users:           {r['total_users']} total, {r['banned']} banned
  Active sessions: {r['sessions']}
  Trusted hashes:  {r['hashes']} {'(integrity check ENABLED)' if r['hashes'] > 0 else '(integrity check disabled)'}
""")


def cmd_issue(con: sqlite3.Connection, args):
    tier   = tier_id(args.tier)
    days   = getattr(args, "days", 0) or TIER_DAYS[tier]
    note   = getattr(args, "note", "") or ""
    now    = int(time.time())
    exp    = expires_at(tier, days, now)

    for attempt in range(5):
        key = generate_key()
        try:
            con.execute(
                "INSERT INTO licenses(key,tier,issued_at,expires_at,note) VALUES(?,?,?,?,?)",
                (key, tier, now, exp, note)
            )
            con.commit()
            exp_str = fmt_ts(exp)
            print(f"  {TIER_NAMES[tier]} license:  {key}  (expires {exp_str})")
            return
        except sqlite3.IntegrityError:
            continue  # key collision — astronomically rare
    print("ERROR: failed to generate unique key")
    sys.exit(1)


def cmd_activate(con: sqlite3.Connection, args):
    key  = args.key.upper()
    hwid = args.hwid.lower()
    now  = int(time.time())

    row = con.execute("SELECT * FROM licenses WHERE key=?", (key,)).fetchone()
    if row is None:
        print(f"ERROR: key not found: {key}")
        sys.exit(1)
    if row["hwid_hash"] and row["hwid_hash"] != hwid:
        print(f"ERROR: key already bound to {fmt_hwid(row['hwid_hash'])}")
        sys.exit(1)
    if row["expires_at"] != 0 and row["expires_at"] < now:
        print(f"ERROR: key expired {fmt_ts(row['expires_at'])}")
        sys.exit(1)

    # Bind key to HWID
    if not row["hwid_hash"]:
        con.execute("UPDATE licenses SET hwid_hash=? WHERE key=?", (hwid, key))
    # Upsert user
    con.execute("""
        INSERT INTO users(hwid_hash, license_key, created_at, last_seen)
        VALUES(?,?,?,?)
        ON CONFLICT(hwid_hash) DO UPDATE
        SET license_key=excluded.license_key, last_seen=excluded.last_seen
    """, (hwid, key, now, now))
    con.commit()
    print(f"  Activated {key} -> {fmt_hwid(hwid)}")


def cmd_ban(con: sqlite3.Connection, args):
    hwid   = args.hwid.lower()
    reason = " ".join(args.reason) if args.reason else "admin ban"
    now    = int(time.time())

    # Ensure user row exists (might be a pre-emptive ban before first connection)
    con.execute("""
        INSERT OR IGNORE INTO users(hwid_hash, created_at, last_seen)
        VALUES(?,?,?)
    """, (hwid, now, now))
    con.execute(
        "UPDATE users SET is_banned=1, ban_reason=? WHERE hwid_hash=?",
        (reason, hwid)
    )
    con.commit()
    print(f"  Banned {fmt_hwid(hwid)}: {reason}")


def cmd_unban(con: sqlite3.Connection, args):
    hwid = args.hwid.lower()
    con.execute(
        "UPDATE users SET is_banned=0, ban_reason='' WHERE hwid_hash=?", (hwid,)
    )
    con.commit()
    print(f"  Unbanned {fmt_hwid(hwid)}")


def cmd_list_licenses(con: sqlite3.Connection, _args):
    now  = int(time.time())
    rows = con.execute(
        "SELECT * FROM licenses ORDER BY issued_at DESC"
    ).fetchall()
    print(f"  {'KEY':<22}  {'TIER':<10}  {'EXPIRES':<22}  {'HWID'}")
    print("  " + "-" * 80)
    for r in rows:
        expired = "EXPIRED" if r["expires_at"] != 0 and r["expires_at"] < now else ""
        hwid    = fmt_hwid(r["hwid_hash"]) if r["hwid_hash"] else "(unbound)"
        exp     = fmt_ts(r["expires_at"])
        tier    = TIER_NAMES.get(r["tier"], str(r["tier"]))
        print(f"  {r['key']:<22}  {tier:<10}  {exp:<22}  {hwid}  {expired}")


def cmd_list_users(con: sqlite3.Connection, _args):
    rows = con.execute(
        "SELECT * FROM users ORDER BY last_seen DESC"
    ).fetchall()
    print(f"  {'HWID':<20}  {'LICENSE':<12}  {'LAST SEEN':<22}  STATUS")
    print("  " + "-" * 80)
    for r in rows:
        status = f"BANNED({r['ban_reason'][:20]})" if r["is_banned"] else "ok"
        key    = r["license_key"][:8] + "..." if r["license_key"] else "(none)"
        print(f"  {fmt_hwid(r['hwid_hash']):<20}  {key:<12}  {fmt_ts(r['last_seen']):<22}  {status}")


def cmd_purchases(con: sqlite3.Connection, args):
    hwid = args.hwid.lower()
    rows = con.execute(
        "SELECT * FROM purchases WHERE hwid_hash=? ORDER BY purchased_at DESC", (hwid,)
    ).fetchall()
    if not rows:
        print(f"  No purchases for {fmt_hwid(hwid)}")
        return
    for r in rows:
        print(f"  id={r['id']}  {r['license_key']}  ${r['amount_cents']/100:.2f}"
              f"  {fmt_ts(r['purchased_at'])}  {r['note']}")


def cmd_add_hash(con: sqlite3.Connection, args):
    h    = args.hash.lower()
    note = " ".join(args.note) if args.note else ""
    con.execute(
        "INSERT OR IGNORE INTO trusted_hashes(hash,note,added_at) VALUES(?,?,?)",
        (h, note, int(time.time()))
    )
    con.commit()
    print(f"  Trusted hash added: {h[:16]}...")


def cmd_rm_hash(con: sqlite3.Connection, args):
    h = args.hash.lower()
    con.execute("DELETE FROM trusted_hashes WHERE hash=?", (h,))
    con.commit()
    print(f"  Hash removed")


def cmd_list_hashes(con: sqlite3.Connection, _args):
    rows = con.execute("SELECT * FROM trusted_hashes ORDER BY added_at").fetchall()
    if not rows:
        print("  (no trusted hashes — integrity check disabled)")
        return
    for r in rows:
        print(f"  {r['hash'][:16]}...  added={fmt_ts(r['added_at'])}  note={r['note']}")


def cmd_prune(con: sqlite3.Connection, _args):
    cutoff = int(time.time()) - 7200
    cur    = con.execute("DELETE FROM sessions WHERE last_heartbeat < ?", (cutoff,))
    con.commit()
    print(f"  Pruned {cur.rowcount} stale session(s)")


def cmd_watch(con: sqlite3.Connection, _args):
    """Tail the users table for new entries."""
    print("  Watching for new connections (Ctrl-C to stop)...\n")
    seen = set(r[0] for r in con.execute("SELECT hwid_hash FROM users").fetchall())
    try:
        while True:
            time.sleep(2)
            rows = con.execute(
                "SELECT hwid_hash, last_seen, is_banned, license_key"
                " FROM users ORDER BY last_seen DESC LIMIT 50"
            ).fetchall()
            for r in rows:
                hwid = r["hwid_hash"]
                if hwid not in seen:
                    seen.add(hwid)
                    status = "BANNED" if r["is_banned"] else "auth'd" if r["license_key"] else "rejected"
                    print(f"  [{fmt_ts(r['last_seen'])}] NEW  {fmt_hwid(hwid)}  {status}")
    except KeyboardInterrupt:
        print("\n  Watch stopped.")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="SparkyServer admin tool")
    ap.add_argument("db",      help="Path to sparky.db")
    ap.add_argument("command", help="Command")
    ap.add_argument("args",    nargs=argparse.REMAINDER)
    opts = ap.parse_args()

    con = connect(opts.db)
    ensure_schema(con)

    cmd   = opts.command.lower()
    rest  = opts.args

    # Sub-parse per command
    sub = argparse.ArgumentParser()

    if cmd in ("gen-key", "genkey"):
        cmd_gen_key(None, None)

    elif cmd == "status":
        cmd_status(con, None)

    elif cmd == "issue":
        sub.add_argument("tier", help="daily|weekly|monthly|lifetime|1|2|3|4")
        sub.add_argument("--days", type=int, default=0)
        sub.add_argument("--note", default="")
        cmd_issue(con, sub.parse_args(rest))

    elif cmd == "activate":
        sub.add_argument("key"); sub.add_argument("hwid")
        cmd_activate(con, sub.parse_args(rest))

    elif cmd == "ban":
        sub.add_argument("hwid"); sub.add_argument("reason", nargs="*")
        cmd_ban(con, sub.parse_args(rest))

    elif cmd == "unban":
        sub.add_argument("hwid")
        cmd_unban(con, sub.parse_args(rest))

    elif cmd == "list-licenses":
        cmd_list_licenses(con, None)

    elif cmd == "list-users":
        cmd_list_users(con, None)

    elif cmd == "purchases":
        sub.add_argument("hwid")
        cmd_purchases(con, sub.parse_args(rest))

    elif cmd == "add-hash":
        sub.add_argument("hash"); sub.add_argument("note", nargs="*")
        cmd_add_hash(con, sub.parse_args(rest))

    elif cmd == "rm-hash":
        sub.add_argument("hash")
        cmd_rm_hash(con, sub.parse_args(rest))

    elif cmd == "list-hashes":
        cmd_list_hashes(con, None)

    elif cmd == "prune":
        cmd_prune(con, None)

    elif cmd == "watch":
        cmd_watch(con, None)

    else:
        print(f"Unknown command: {cmd}")
        print("Commands: status issue activate ban unban list-licenses list-users"
              " purchases add-hash rm-hash list-hashes prune watch")
        sys.exit(1)

    con.close()


if __name__ == "__main__":
    main()
