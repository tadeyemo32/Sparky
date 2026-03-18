# SparkyServer — Component Context

## Role
SparkyServer is the **auth + binary delivery service**.
It authenticates loaders, manages licenses/subscriptions, and streams an
encrypted DLL (SparkyCore.dll) to authorised clients — the payload is never
stored on client disk.

## Source Layout
```
SparkyServer/
├── include/
│   ├── Database.h        ← SQLite3 CRUD wrapper (licenses, users, sessions, purchases)
│   └── LicenseManager.h  ← Key generation + activation
├── src/
│   ├── main.cpp          ← TCP server, auth flow, DLL streaming, admin console
│   ├── Database.cpp
│   └── LicenseManager.cpp
└── CMakeLists.txt        ← links sqlite3 (deps/sqlite3/), ws2_32, advapi32
```

## Database Schema
```
licenses  (key TEXT PK, tier INT, issued_at INT, expires_at INT, hwid_hash TEXT)
users     (hwid_hash TEXT PK, license_key TEXT, created_at INT, last_seen INT,
           ban_reason TEXT)
sessions  (token_hex TEXT PK, hwid_hash TEXT, created_at INT, last_heartbeat INT)
purchases (id INT PK, hwid_hash TEXT, license_key TEXT, amount_cents INT,
           purchased_at INT, note TEXT)
```

## Auth Flow
```
Loader                          Server
  │── Hello (HWID hash + build) ──▶│
  │                                │  1. Check build ID == CURRENT_BUILD
  │                                │  2. TouchUser (record / update last_seen)
  │                                │  3. IsAuthorised: user exists + not banned +
  │                                │     license bound + not expired
  │◀── AuthFail ───────────────────│  (if rejected)
  │◀── AuthOk (session token) ─────│  (if accepted)
  │                                │  4. Generate 16-byte CryptGenRandom token
  │                                │  5. Store session in DB
  │◀── Config (optional blob) ─────│
  │◀── BinaryReady (size/chunks) ──│
  │◀── BinaryChunk × N ────────────│  DLL XOR'd with DeriveKey(token, salt=1)
  │◀── BinaryEnd ──────────────────│
  │── Heartbeat ──────────────────▶│
  │◀── Ack ─────────────────────── │  TouchSession in DB
  │  ...                           │
```

## License Key Format
`XXXX-XXXX-XXXX-XXXX` — 16 chars from charset `A-Z 2-9` (no O/I/0/1)
80 bits of entropy from `CryptGenRandom`.

## Admin Console (stdin)
| Command | Description |
|---|---|
| `issue <tier> <days>` | Generate + insert new license (days=0 = perpetual) |
| `activate <key> <hwid>` | Bind license to HWID, create user row |
| `ban <hwid> <reason>` | Ban a user |
| `unban <hwid>` | Remove ban |
| `list-licenses` | Dump all licenses |
| `purchases <hwid>` | Show purchase history |
| `sessions` | Print active session count |
| `prune` | Delete sessions idle > 2 h |

## Deps
| Dep | Source |
|---|---|
| SQLite3 | `deps/sqlite3/` — official amalgamation (`sqlite3.c` + `sqlite3.h`) |
| WinSock2 | system |
| advapi32 | system (CryptGenRandom) |
| Protocol.h | `SparkyLoader/include/Protocol.h` |

## SQLite Dep Setup
Download from https://www.sqlite.org/download.html → "amalgamation" zip.
Extract `sqlite3.c` and `sqlite3.h` into `<repo_root>/deps/sqlite3/`.

## Flaws / TODOs
- [ ] **XOR-only DLL encryption** — replace `XorStream` with AES-256-GCM using
      Windows BCrypt (BCryptEncrypt) for stronger confidentiality.
- [ ] **No TLS** — TCP socket is plaintext; HWID hashes and DLL chunks are
      visible to a network observer. Wrap with schannel or a TLS stub.
- [ ] **No rate limiting** — a brute-force auth loop can hammer HWID checks;
      add per-IP connect throttle.
- [ ] **Admin console is unauthenticated** — stdin only, fine for local server;
      if exposed remotely, add auth.
- [ ] **Session token not verified on Heartbeat** — loader sends Heartbeat but
      doesn't re-prove identity; fine for now since session is per-TCP-connection.
- [ ] **Purchase recording not wired to activation** — `InsertPurchase` exists
      but `ActivateLicense` doesn't call it; wire via a webhook / admin command.
- [ ] **DLL file not hot-reloaded** — server must restart to pick up a new
      SparkyCore.dll; add a `reload` admin command that re-reads the file.
