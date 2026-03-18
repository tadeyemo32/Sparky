# SparkyLoader — Component Context

## Role
The loader is the **user-facing EXE**. It:
1. Shows a minimalist ImGui UI (dark navy / cyan theme).
2. Connects to SparkyServer and authenticates via HWID + build ID.
3. Receives the game DLL **encrypted in RAM** (never written to disk).
4. Decrypts the DLL with the session token.
5. Waits for the target process, then **manually maps** the DLL using
   a stealth pipeline designed to evade the in-game anti-cheat.

## Source Layout
```
SparkyLoader/
├── include/
│   ├── Logger.h        ← thread-safe file + console + OutputDebugString logging
│   ├── ManualMap.h     ← stealth injection interface + MappingData shellcode struct
│   ├── Protocol.h      ← binary wire protocol shared with Server
│   └── UI.h            ← ImGui/GLFW UI state + RunUI()
├── src/
│   ├── main.cpp        ← wWinMain: process watcher, server comms, inject flow
│   ├── Logger.cpp      ← Logger implementation
│   ├── ManualMap.cpp   ← stealth injection implementation
│   └── UI.cpp          ← ImGui render loop + style
├── tests/
│   ├── DummyDll.cpp    ← test DLL (shows MessageBox on inject)
│   └── DummyTarget.cpp ← test process that prints its PID and waits
├── bin/                ← build output
└── CMakeLists.txt
```

## Injection Pipeline (ManualMap)
```
1. NtAllocateVirtualMemory     — allocates image in target (bypasses VirtualAllocEx hook)
2. NtWriteVirtualMemory        — writes sections (bypasses WriteProcessMemory hook)
3. NtProtectVirtualMemory      — per-section RX/RW/RO protections
4. MappingShellcode (APC)      — runs inside target:
     a. Relocations            — fix base delta
     b. IAT via LdrLoadDll     — load imports WITHOUT calling hooked LoadLibrary export
        + LdrGetProcedureAddress
     c. TLS callbacks
     d. DllMain
     e. Overwrite PE headers   — random noise (not zeros — zero pattern is detectable)
5. NtFreeVirtualMemory         — free shellcode stub immediately after
```

## Deps
| Dep     | Location       | Purpose |
|---------|----------------|---------|
| GLFW    | `deps/glfw`    | OpenGL window + input for ImGui |
| ImGui   | `deps/imgui`   | Immediate-mode GUI |
| ntdll   | system         | Nt* API for stealth alloc/write |
| crypt32 | system         | SHA-256 HWID, (future) AES |
| ws2_32  | system         | Winsock for server comms |
| psapi   | system         | Module enumeration |

## Flaws / TODOs
- [ ] **LoadLibrary still referenced indirectly**: `std::filesystem`, `std::thread`,
      and some MinGW runtime paths call `LoadLibraryExW` internally. Replace:
      - `std::filesystem::exists` → manual `CreateFileW` existence check
      - `std::thread` → raw `CreateThread` (or ntdll `RtlCreateUserThread`)
- [ ] **Shellcode not obfuscated in loader binary**: The shellcode function sits in
      `.text` and a mem scanner could find its byte pattern in the loader EXE itself.
      Fix: encrypt shellcode bytes in a data array, decrypt at runtime before writing.
- [ ] **APC only fires on alertable thread**: If the game has no alertable threads
      at injection time, the APC never executes. Add fallback: hijack a thread's
      RIP register via `GetThreadContext` / `SetThreadContext`.
- [ ] **Session XOR key = first 8 bytes of token**: weak derivation. Replace with
      HKDF-SHA256 derived key once CryptAPI is wired up properly.
- [ ] **No retry logic** on server connection failure.
- [ ] **DLL path** currently hardcoded as `SparkyCore.dll` in UIState default.
      Should be loaded from a local config or received from server.
- [ ] **UI window is fixed 480×560** — add DPI awareness for high-res screens.
- [ ] **No process elevation check** — injection requires PROCESS_ALL_ACCESS which
      fails if target runs elevated and loader does not.
