#!/usr/bin/env python3
"""
admin.py — SparkyServer admin tool (PostgreSQL backend).

Connects to the same PostgreSQL database the C++ server uses.
Connection string is loaded from the same sources as KeyVault.h:
  1. SPARKY_PG_CONNSTR  env var  (full libpq connection string)
  2. SPARKY_PG_CONNFILE  env var  (path to a file containing the string)
  3. sparky.connstr              (file next to CWD)

Install dependency:
  pip install psycopg2-binary

Usage:
    python admin.py <command> [args...]

Examples:
    python admin.py status
    python admin.py gen-token                     # generate a random 32-byte hex token
    python admin.py issue weekly
    python admin.py issue daily --days 1
    python admin.py issue lifetime --note "VIP"
    python admin.py activate <KEY> <hwid_hex>
    python admin.py ban  <hwid_hex> "caught cheating"
    python admin.py unban <hwid_hex>
    python admin.py list-licenses
    python admin.py list-users
    python admin.py purchases <hwid_hex>
    python admin.py add-hash <sha256_hex> [note]
    python admin.py rm-hash <sha256_hex>
    python admin.py list-hashes
    python admin.py prune
    python admin.py watch                         # live-tail new connections
"""

import sys
import os
import time
import random
import argparse
from datetime import datetime, timezone

try:
    import psycopg2
    import psycopg2.extras
    import psycopg2.errors
except ImportError:
    print("ERROR: psycopg2 not installed.  Run:  pip install psycopg2-binary",
          file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# Connection string loading — mirrors KeyVault.h resolution order
# ---------------------------------------------------------------------------
def _load_connstr() -> str:
    connstr = None

    if v := os.environ.get("SPARKY_PG_CONNSTR"):
        connstr = v.strip()
    elif kf := os.environ.get("SPARKY_PG_CONNFILE"):
        with open(kf) as f:
            connstr = f.readline().strip()
    elif os.path.exists("sparky.connstr"):
        with open("sparky.connstr") as f:
            connstr = f.readline().strip()

    if not connstr:
        raise RuntimeError(
            "No PostgreSQL connection string found.\n"
            "  Set SPARKY_PG_CONNSTR='host=localhost port=5432 dbname=sparky "
            "user=sparky password=...'\n"
            "  or SPARKY_PG_CONNFILE=<path>\n"
            "  or place sparky.connstr next to admin.py"
        )
    return connstr


def connect() -> psycopg2.extensions.connection:
    connstr = _load_connstr()
    con = psycopg2.connect(connstr)
    return con


def cursor(con) -> psycopg2.extras.RealDictCursor:
    """Return a dict cursor so rows are accessible by column name."""
    return con.cursor(cursor_factory=psycopg2.extras.RealDictCursor)


# ---------------------------------------------------------------------------
# Key generation (mirrors C++ LicenseManager)
# ---------------------------------------------------------------------------
KEY_CHARSET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"  # no O/I/0/1


def generate_key() -> str:
    """Generate a XXXX-XXXX-XXXX-XXXX license key (80-bit entropy)."""
    raw   = os.urandom(10)
    value = int.from_bytes(raw, "big")
    chars = []
    for _ in range(16):
        chars.append(KEY_CHARSET[value & 0x1F])
        value >>= 5
    chars.reverse()
    return "-".join("".join(chars[i:i + 4]) for i in range(0, 16, 4))


# ---------------------------------------------------------------------------
# Tier helpers
# ---------------------------------------------------------------------------
TIER_NAMES   = {1: "Daily", 2: "Weekly", 3: "Monthly", 4: "Lifetime"}
TIER_DAYS    = {1: 1,       2: 7,        3: 30,         4: 0}
TIER_BY_NAME = {v.lower(): k for k, v in TIER_NAMES.items()}


def tier_id(name_or_int: str) -> int:
    try:
        return int(name_or_int)
    except ValueError:
        t = TIER_BY_NAME.get(name_or_int.lower())
        if t is None:
            raise ValueError(
                f"Unknown tier '{name_or_int}'. Use: daily weekly monthly lifetime")
        return t


def expires_at(tier: int, days_override: int = 0, now: int = 0) -> int:
    now  = now or int(time.time())
    days = days_override or TIER_DAYS[tier]
    return 0 if days == 0 else now + days * 86400


# ---------------------------------------------------------------------------
# Formatting helpers
# ---------------------------------------------------------------------------
def fmt_ts(ts: int) -> str:
    if ts == 0:
        return "never"
    return datetime.fromtimestamp(ts, tz=timezone.utc).strftime("%Y-%m-%d %H:%M UTC")


def fmt_hwid(h: str) -> str:
    return h[:16] + "..." if len(h) > 16 else h


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------
def cmd_gen_token(_con, _args):
    """Print a cryptographically random 32-byte hex token."""
    token = os.urandom(32).hex()
    print(f"\n  {token}\n")
    print("  Use this as an application secret or API key.")
    print("  For the DB password, set it in your PostgreSQL connection string.\n")


def cmd_status(con, _args):
    cur = cursor(con)
    cur.execute("""
        SELECT
            (SELECT COUNT(*) FROM licenses)                       AS total_keys,
            (SELECT COUNT(*) FROM licenses WHERE hwid_hash != '') AS bound_keys,
            (SELECT COUNT(*) FROM users)                          AS total_users,
            (SELECT COUNT(*) FROM users WHERE is_banned = 1)      AS banned,
            (SELECT COUNT(*) FROM sessions)                       AS sessions,
            (SELECT COUNT(*) FROM trusted_hashes)                 AS hashes
    """)
    r = cur.fetchone()
    now = int(time.time())
    cur.execute(
        "SELECT COUNT(*) AS n FROM licenses WHERE expires_at > 0 AND expires_at < %s",
        (now,)
    )
    expired = cur.fetchone()["n"]
    print(f"""
Server database status
  Licenses:        {r['total_keys']} total, {r['bound_keys']} bound, {expired} expired
  Users:           {r['total_users']} total, {r['banned']} banned
  Active sessions: {r['sessions']}
  Trusted hashes:  {r['hashes']} {'(integrity check ENABLED)' if r['hashes'] > 0 else '(integrity check disabled)'}
""")


def cmd_issue(con, args):
    tier  = tier_id(args.tier)
    days  = getattr(args, "days", 0) or TIER_DAYS[tier]
    note  = getattr(args, "note", "") or ""
    now   = int(time.time())
    exp   = expires_at(tier, days, now)
    cur   = cursor(con)

    for _ in range(5):
        key = generate_key()
        try:
            cur.execute(
                "INSERT INTO licenses(key,tier,issued_at,expires_at,note)"
                " VALUES(%s,%s,%s,%s,%s)",
                (key, tier, now, exp, note)
            )
            con.commit()
            print(f"  {TIER_NAMES[tier]} license:  {key}  (expires {fmt_ts(exp)})")
            return
        except psycopg2.errors.UniqueViolation:
            con.rollback()
            continue   # key collision — astronomically rare; retry

    print("ERROR: failed to generate unique key after 5 attempts")
    sys.exit(1)


def cmd_activate(con, args):
    key  = args.key.upper()
    hwid = args.hwid.lower()
    now  = int(time.time())
    cur  = cursor(con)

    cur.execute("SELECT * FROM licenses WHERE key=%s", (key,))
    row = cur.fetchone()
    if row is None:
        print(f"ERROR: key not found: {key}"); sys.exit(1)
    if row["hwid_hash"] and row["hwid_hash"] != hwid:
        print(f"ERROR: key already bound to {fmt_hwid(row['hwid_hash'])}"); sys.exit(1)
    if row["expires_at"] != 0 and row["expires_at"] < now:
        print(f"ERROR: key expired {fmt_ts(row['expires_at'])}"); sys.exit(1)

    if not row["hwid_hash"]:
        cur.execute("UPDATE licenses SET hwid_hash=%s WHERE key=%s", (hwid, key))

    cur.execute("""
        INSERT INTO users(hwid_hash, license_key, created_at, last_seen)
        VALUES(%s,%s,%s,%s)
        ON CONFLICT(hwid_hash) DO UPDATE
        SET license_key=EXCLUDED.license_key, last_seen=EXCLUDED.last_seen
    """, (hwid, key, now, now))

    con.commit()
    print(f"  Activated {key} -> {fmt_hwid(hwid)}")


def cmd_ban(con, args):
    hwid   = args.hwid.lower()
    reason = " ".join(args.reason) if args.reason else "admin ban"
    now    = int(time.time())
    cur    = cursor(con)

    # Pre-emptive ban: ensure user row exists before first connection
    cur.execute("""
        INSERT INTO users(hwid_hash, created_at, last_seen)
        VALUES(%s,%s,%s)
        ON CONFLICT(hwid_hash) DO NOTHING
    """, (hwid, now, now))
    cur.execute(
        "UPDATE users SET is_banned=1, ban_reason=%s WHERE hwid_hash=%s",
        (reason, hwid)
    )
    con.commit()
    print(f"  Banned {fmt_hwid(hwid)}: {reason}")


def cmd_unban(con, args):
    hwid = args.hwid.lower()
    cur  = cursor(con)
    cur.execute(
        "UPDATE users SET is_banned=0, ban_reason='' WHERE hwid_hash=%s", (hwid,)
    )
    con.commit()
    print(f"  Unbanned {fmt_hwid(hwid)}")


def cmd_list_licenses(con, _args):
    cur = cursor(con)
    cur.execute("SELECT * FROM licenses ORDER BY issued_at DESC")
    rows = cur.fetchall()
    now  = int(time.time())
    print(f"  {'KEY':<22}  {'TIER':<10}  {'EXPIRES':<22}  {'HWID'}")
    print("  " + "-" * 80)
    for r in rows:
        expired = "EXPIRED" if r["expires_at"] != 0 and r["expires_at"] < now else ""
        hwid    = fmt_hwid(r["hwid_hash"]) if r["hwid_hash"] else "(unbound)"
        exp     = fmt_ts(r["expires_at"])
        tier    = TIER_NAMES.get(r["tier"], str(r["tier"]))
        print(f"  {r['key']:<22}  {tier:<10}  {exp:<22}  {hwid}  {expired}")


def cmd_list_users(con, _args):
    cur = cursor(con)
    cur.execute("SELECT * FROM users ORDER BY last_seen DESC")
    rows = cur.fetchall()
    print(f"  {'HWID':<20}  {'LICENSE':<12}  {'LAST SEEN':<22}  STATUS")
    print("  " + "-" * 80)
    for r in rows:
        status = f"BANNED({r['ban_reason'][:20]})" if r["is_banned"] else "ok"
        key    = r["license_key"][:8] + "..." if r["license_key"] else "(none)"
        print(f"  {fmt_hwid(r['hwid_hash']):<20}  {key:<12}"
              f"  {fmt_ts(r['last_seen']):<22}  {status}")


def cmd_purchases(con, args):
    hwid = args.hwid.lower()
    cur  = cursor(con)
    cur.execute(
        "SELECT * FROM purchases WHERE hwid_hash=%s ORDER BY purchased_at DESC",
        (hwid,)
    )
    rows = cur.fetchall()
    if not rows:
        print(f"  No purchases for {fmt_hwid(hwid)}"); return
    for r in rows:
        print(f"  id={r['id']}  {r['license_key']}  ${r['amount_cents'] / 100:.2f}"
              f"  {fmt_ts(r['purchased_at'])}  {r['note']}")


def cmd_add_hash(con, args):
    h    = args.hash.lower()
    note = " ".join(args.note) if args.note else ""
    cur  = cursor(con)
    cur.execute(
        "INSERT INTO trusted_hashes(hash,note,added_at) VALUES(%s,%s,%s)"
        " ON CONFLICT(hash) DO NOTHING",
        (h, note, int(time.time()))
    )
    con.commit()
    print(f"  Trusted hash added: {h[:16]}...")


def cmd_rm_hash(con, args):
    h   = args.hash.lower()
    cur = cursor(con)
    cur.execute("DELETE FROM trusted_hashes WHERE hash=%s", (h,))
    con.commit()
    print("  Hash removed")


def cmd_list_hashes(con, _args):
    cur = cursor(con)
    cur.execute("SELECT * FROM trusted_hashes ORDER BY added_at")
    rows = cur.fetchall()
    if not rows:
        print("  (no trusted hashes — integrity check disabled)"); return
    for r in rows:
        print(f"  {r['hash'][:16]}...  added={fmt_ts(r['added_at'])}  note={r['note']}")


def cmd_prune(con, _args):
    cutoff = int(time.time()) - 7200
    cur    = cursor(con)
    cur.execute("DELETE FROM sessions WHERE last_heartbeat < %s", (cutoff,))
    n = cur.rowcount
    con.commit()
    print(f"  Pruned {n} stale session(s)")


def cmd_watch(con, _args):
    """Live-tail new user connections by polling the users table every 2 seconds."""
    print("  Watching for new connections (Ctrl-C to stop)...\n")
    cur  = cursor(con)
    cur.execute("SELECT hwid_hash FROM users")
    seen = {r["hwid_hash"] for r in cur.fetchall()}
    try:
        while True:
            time.sleep(2)
            # Commit to close the current snapshot so we see rows written
            # by the C++ server since our last query.
            con.commit()
            cur = cursor(con)
            cur.execute(
                "SELECT hwid_hash, last_seen, is_banned, license_key"
                " FROM users ORDER BY last_seen DESC LIMIT 50"
            )
            for r in cur.fetchall():
                hwid = r["hwid_hash"]
                if hwid not in seen:
                    seen.add(hwid)
                    status = ("BANNED"   if r["is_banned"]
                              else "auth'd" if r["license_key"]
                              else "rejected")
                    print(f"  [{fmt_ts(r['last_seen'])}] NEW  {fmt_hwid(hwid)}  {status}")
    except KeyboardInterrupt:
        print("\n  Watch stopped.")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(
        description="SparkyServer admin tool",
        epilog="Connection string: set SPARKY_PG_CONNSTR or place sparky.connstr here."
    )
    ap.add_argument("command", help="Command to run")
    ap.add_argument("args",    nargs=argparse.REMAINDER)
    opts = ap.parse_args()

    cmd  = opts.command.lower()
    rest = opts.args

    # gen-token doesn't need a DB connection
    if cmd in ("gen-token", "gentoken"):
        cmd_gen_token(None, None)
        return

    try:
        con = connect()
    except Exception as e:
        print(f"ERROR: cannot connect to PostgreSQL: {e}", file=sys.stderr)
        sys.exit(1)

    sub = argparse.ArgumentParser()

    if cmd == "status":
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
