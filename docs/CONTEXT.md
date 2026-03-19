# Sparky — Global Project Context

## What This Is
Sparky is a three-component runtime mod framework built for an **offline Marvel Rivals clone**
used to **test a custom anti-cheat system**. All three components work together:

```
SparkyServer  ──(TCP binary protocol)──►  SparkyLoader  ──(APC injection)──►  SparkyCore.dll
  auth + paid check                         manual mapper                       UE5 mod runtime
  encrypt + stream DLL                      decrypt in RAM                      in-game features
  no DLL on disk                            never writes DLL to disk
```

## Repository Layout
```
Sparky/
├── deps/                   ← shared submodules (moved from SparkyLoader/deps/)
│   ├── glfw/               ← window + OpenGL context (Loader UI)
│   └── imgui/              ← immediate-mode UI (Loader UI)
│
├── SparkyLoader/           ← EXE: authenticates, receives encrypted DLL, injects
├── SparkyServer/           ← EXE: auth server, paid check, encrypts + streams DLL
├── SparkyCore/             ← DLL: UE5 mod runtime injected into the game
│
├── CONTEXT.md              ← this file
└── .gitmodules
```

## Component Roles

| Component     | Language | Output         | Role |
|---------------|----------|----------------|------|
| SparkyServer  | C++20    | Server EXE     | Auth, paid status, encrypted DLL delivery |
| SparkyLoader  | C++20    | Loader EXE     | UI, decrypt DLL in RAM, stealth injection |
| SparkyCore    | C++20    | Client DLL     | UE5 mod runtime (aimbot, ESP, etc.) |

## Security Architecture
1. **DLL never touches disk** — Server streams it encrypted; Loader maps from RAM.
2. **Per-session unique encryption** — DLL XOR-encrypted with session token (unique per auth).
3. **Stealth injection** — NtAllocateVirtualMemory + APC (no LoadLibrary, no CreateRemoteThread).
4. **Shellcode obfuscation** — Shellcode XOR-encrypted before writing to target memory.
5. **Header erasure** — PE headers overwritten with random noise after DllMain.

## Shared Protocol
`SparkyLoader/include/Protocol.h` — binary wire format shared by Loader ↔ Server.
Magic: `0x53504B59` ("SPKY"), wire XOR + CRC-32.

## Build Order
1. Build `SparkyServer` first (no deps).
2. Build `SparkyLoader` (needs GLFW + ImGui submodules: `git submodule update --init --recursive`).
3. Build `SparkyCore` (needs MSVC or MinGW targeting Windows x64).

## Global TODOs
- [ ] Implement AES-256 (via Windows CryptAPI) for DLL encryption instead of XOR stream
- [ ] Add TLS wrapping to the server TCP socket
- [ ] CI: cross-compile all three with MinGW toolchain on GitHub Actions
- [ ] Unify CMake via a root `CMakeLists.txt` that adds all three subdirs
- [ ] Write integration test: server → loader → DummyTarget → DummyDll
