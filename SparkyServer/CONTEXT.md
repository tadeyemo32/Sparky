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
│   ├── Database.h        ← SQLite3/SQLCipher CRUD (licenses, users, sessions, purchases, trusted_hashes)
│   ├── LicenseManager.h  ← Key generation + activation
│   ├── RateLimiter.h     ← Header-only sliding-window per-IP throttle
│   └── KeyVault.h        ← DB key loading from env / key file
├── src/
│   ├── main.cpp          ← TCP server, auth flow, DLL streaming, admin console
│   ├── Database.cpp
│   └── LicenseManager.cpp
├── admin.py              ← Python admin CLI (SQLCipher-aware)
├── scripts/
│   ├── setup.sh          ← VPS hardening: user, ufw, systemd install, sshd
│   └── sparky.service    ← Systemd unit with full sandboxing
└── CMakeLists.txt        ← option(SPARKY_SQLCIPHER); links sqlite3 or sqlcipher
```

## Database Schema
```
licenses       (key TEXT PK, tier INT, issued_at INT, expires_at INT, hwid_hash TEXT)
users          (hwid_hash TEXT PK, license_key TEXT, created_at INT, last_seen INT,
                is_banned INT, ban_reason TEXT, loader_hash TEXT)
sessions       (token_hex TEXT PK, hwid_hash TEXT, created_at INT, last_heartbeat INT)
purchases      (id INT PK, hwid_hash TEXT, license_key TEXT, amount_cents INT,
                purchased_at INT, note TEXT)
trusted_hashes (hash_hex TEXT PK, added_at INT, note TEXT)
```

SQLCipher: AES-256-CBC, PBKDF2-SHA512, 256 000 iterations, 65 KB page size.
Key loaded by `KeyVault::LoadDbKey()`: `SPARKY_DB_KEY` env → `SPARKY_DB_KEYFILE` env → `sparky.key` file.

## Wire Protocol v3
`Protocol.h` (shared with loader at `SparkyLoader/include/Protocol.h`).
Magic `0x53504B59` ("SPKY"), version byte `0x03`, all integers little-endian.

### MsgHeader (12 bytes, packed)
```
uint32_t Magic | uint8_t Version | MsgType Type | uint16_t Length | uint8_t Pad[4]
```
All messages (except the plain AuthOk) have their header XOR'd with `hdrKey`.
CRC-32 is appended to every payload for integrity.

### Auth Flow + Bootstrap Rule
```
Loader                               Server
  │── Hello (plain) ────────────────▶│  HwidHash[32] + BuildId + LoaderHash[32]
  │                                  │  1. Build ID == CURRENT_BUILD
  │                                  │  2. LoaderHash in trusted_hashes (if enabled)
  │                                  │  3. IsAuthorised: not banned, license valid
  │◀── AuthFail (plain) ─────────────│  (rejected)
  │◀── AuthOk (PLAIN) ───────────────│  SessionToken[16] + ExpiresAt
  │                                  │  ← CRITICAL: sent before deriving hdrKey
  │  [loader derives hdrKey/dllKey]  │  [server derives hdrKey/dllKey]
  │◀── Config (encrypted) ───────────│  opaque config blob (optional)
  │◀── BinaryReady (encrypted) ──────│  TotalBytes / ChunkSize / NumChunks / ChunksPerHB
  │◀── BinaryChunk × N (encrypted) ──│  XorStream(chunk, rollingKey)
  │── Heartbeat ────────────────────▶│  Nonce[16] (CryptRandBytes) after each batch
  │◀── Ack ──────────────────────────│  rollingKey = RollKey(rollingKey, nonce)
  │  ... (repeats per batch) ...     │
  │◀── BinaryEnd ────────────────────│
  │── Heartbeat (zero nonce) ────────▶│  keep-alive every 25 s (background thread)
  │◀── Ack ──────────────────────────│
```

**Bootstrap rule**: `AuthOk` is ALWAYS sent with `hdrKey = 0` (plain). The
loader reads it unencrypted to extract the token, then both sides call
`DeriveKey(token, 0)` → `hdrKey` and `DeriveKey(token, 1)` → `dllKey`.
Violating this rule causes a decryption deadlock.

### Rolling Key
After every `ChunksPerHeartbeat` (8) chunks the loader:
1. Generates a 16-byte cryptographic nonce via `CryptGenRandom`.
2. Sends `HeartbeatPayload{Nonce}`.
3. Receives `Ack`.
4. Both sides advance: `rollingKey = RollKey(rollingKey, nonce)`.

`RollKey(key, nonce) = SHA-256(key[8 LE] || nonce[16])[0:8]` — inline
single-block SHA-256 in `Protocol.h`, no external deps.

### Key Derivation
```
hdrKey = DeriveKey(token, salt=0)   // encrypts MsgHeaders
dllKey = DeriveKey(token, salt=1)   // initial rollingKey for DLL chunks
DeriveKey: XOR fold of token bytes ^ (salt<<32|salt)
```

## License Key Format
`XXXX-XXXX-XXXX-XXXX` — 16 chars from charset `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`
(no O/I/0/1). 80 bits of entropy from `CryptGenRandom`, base-32 encoded.

Tiers: 1=Daily (1 d), 2=Weekly (7 d), 3=Monthly (30 d), 4=Lifetime (perpetual).

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
| `add-hash <hex> [note]` | Add loader hash to trusted set |
| `rm-hash <hex>` | Remove trusted hash |

Python admin tool (`admin.py`) mirrors all commands with SQLCipher support.

## Rate Limiter
`RateLimiter.h` — sliding-window per-IP:
- Soft reject: 10 hits / 60 s
- Hard ban: 30 hits / 60 s (suggests iptables DROP)
- `Prune()` clears stale entries; `Unban(ip)` for admin recovery

## Infrastructure Hardening
- `scripts/setup.sh`: creates `sparky` system user (no login shell), `chmod 400`
  key file, ufw `deny incoming` + `limit 7777/tcp`, installs systemd service
- `scripts/sparky.service`: `NoNewPrivileges`, `PrivateTmp`, `ProtectSystem=strict`,
  `MemoryDenyWriteExecute`, `RestrictAddressFamilies=AF_INET AF_INET6`,
  `SystemCallFilter=@system-service @network-io @basic-io @io-event`

## Deps
| Dep | Source |
|---|---|
| SQLite3 | `deps/sqlite3/` — official amalgamation |
| SQLCipher | `deps/sqlcipher/` — enable with `-DSPARKY_SQLCIPHER=ON` |
| WinSock2 | system |
| advapi32 | system (CryptGenRandom) |
| Protocol.h | `SparkyLoader/include/Protocol.h` |

## TODOs
- [ ] **AES-256-GCM** — replace `XorStream` with BCrypt AES-256-GCM for stronger
      DLL confidentiality.
- [ ] **TLS** — TCP is plaintext; HWID hashes and chunks visible on wire. Wrap
      with SChannel or a lightweight TLS stub.
- [ ] **Wire `InsertPurchase`** — `ActivateLicense` doesn't call it; wire via
      webhook or `purchase` admin command.
- [ ] **`reload` admin command** — hot-reload SparkyCore.dll without restart.
- [ ] **SparkyCore feature implementations** — Aimbot, ESP, movement mods for
      Marvel Rivals clone UE5 heroes (see SparkyCore CONTEXT.md).
