# SparkyServer — Component Context

## Role
The server is the **distribution and access-control gate**. It:
1. Receives a connection from SparkyLoader.
2. Validates HWID (machine fingerprint) and build ID.
3. Checks **paid/active subscription status** for the HWID.
4. If authorised: encrypts the game DLL uniquely with the session token and **streams it** to the loader over the wire.
5. The DLL **never lives on the client's disk** — it exists only in the server's file store and briefly in the loader's RAM.

## Source Layout
```
SparkyServer/
├── src/
│   └── main.cpp        ← entire server: auth, paid check, DLL encryption + streaming
├── bin/                ← build output
├── config.bin          ← (runtime) config blob pushed to clients after auth
├── SparkyCore.dll.enc  ← (runtime) master-encrypted DLL stored on server
├── hwid_whitelist.txt  ← (optional) explicit HWID allowlist; empty = all allowed
├── paid_hwids.txt      ← HWIDs with active subscriptions
└── CMakeLists.txt
```

## Protocol Flow
```
Loader                          Server
  │── Hello (HWID, buildId) ────►│
  │                              │  1. Check build ID matches CURRENT_BUILD
  │                              │  2. Check HWID in paid_hwids.txt
  │                              │  3. Generate 16-byte session token
  │                              │  4. Derive per-session XOR key from token
  │◄─── AuthOk (token) ──────────│
  │◄─── Config (json/bin) ───────│  (optional feature config)
  │◄─── BinaryReady (size) ──────│  DLL size so loader can pre-allocate
  │◄─── BinaryChunk × N ─────────│  encrypted DLL in 4 KB chunks
  │◄─── BinaryEnd ───────────────│
  │── Heartbeat ─────────────────►│  (keep-alive every 30 s while game runs)
```

## DLL Encryption Scheme
```
stored_on_server = AES-256-CBC(master_key, DLL_bytes)   ← at rest
wire_payload     = XOR_stream(session_token, AES_decrypt(master_key, stored))
```
The loader XOR-decrypts with its session token → unique ciphertext per session.
Even if two loaders authenticate simultaneously, their wire payloads differ.

> Current implementation uses XOR stream only (master AES-256 is a TODO).

## Deps
| Dep    | Purpose |
|--------|---------|
| ws2_32 | Winsock TCP server |
| Protocol.h | shared wire format (from SparkyLoader/include/) |

## Flaws / TODOs
- [ ] **No real paid check yet**: `paid_hwids.txt` file is checked but not created
      automatically. Need a payment backend (Stripe webhook → writes to file or DB).
- [ ] **XOR-only encryption**: Should be AES-256-CBC with a server-side master key.
      Add via Windows CryptAPI or a single-header lib (e.g. tiny-AES-c).
- [ ] **DLL streaming not implemented yet**: Currently only sends config blob.
      Add `BinaryReady` / `BinaryChunk` / `BinaryEnd` message types to Protocol.h
      and implement chunked DLL streaming after AuthOk.
- [ ] **No TLS**: wire traffic is XOR-obfuscated only. Wrap with WinSSL / OpenSSL
      for a real deployment.
- [ ] **Single-threaded per-client** `std::thread::detach` is fine for dev but needs
      a proper thread pool (e.g. 64 concurrent users).
- [ ] **No rate-limiting**: brute-force HWID spoofing is possible.
- [ ] **Session tokens not revoked** if client disappears (no heartbeat timeout cleanup).
- [ ] **Binary stored in plaintext** as `SparkyCore.dll` on server file system.
      Must be encrypted at rest before production use.
