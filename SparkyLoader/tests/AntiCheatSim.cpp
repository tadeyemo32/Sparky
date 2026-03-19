// AntiCheatSim.cpp — "Best-of-the-Best" anti-cheat simulator.
//
// This process is the HARDENED TARGET used by the integration test suite.
// It employs three independent detection watchdogs that run continuously
// in background threads, mimicking the core detection engines of EAC/BN:
//
//   1. Thread Watchdog  — flags threads whose start address lands in a
//                         memory region not backed by any loaded module
//                         (the primary indicator of a manually-mapped DLL).
//
//   2. Page Scanner     — walks every MEM_COMMIT page and reports any
//                         private page whose first two bytes are 'MZ',
//                         indicating a hidden PE image in process memory.
//
//   3. .text Integrity  — CRC-32s the host's own .text section at startup
//                         and rechecks it periodically; a mismatch means
//                         someone has patched the code (inline hook).
//
// Output protocol (stdout, line-buffered):
//
//   READY:<pid>                  — emitted once when all watchdogs are up
//   DETECT:<type>:<detail>       — emitted each time a watchdog fires
//   SHUTDOWN                     — emitted just before exit
//
// The named event "Global\SparkyDllSuccess" is waited on; if the injected
// DLL sets it, the process exits cleanly with code 0 so the test harness
// can distinguish "injection succeeded" from "process crashed".
//
// Build:  added as AntiCheatSim target in SparkyLoader/CMakeLists.txt
// Usage:  AntiCheatSim.exe          (no arguments)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <winternl.h>
#include <Psapi.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <vector>
#include <string>
#include <utility>
#include <algorithm>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static std::atomic<bool> g_shutdown{ false };

// Interval in milliseconds between each watchdog scan pass.
static constexpr DWORD SCAN_INTERVAL_MS = 500;

// ---------------------------------------------------------------------------
// Report a detection to stdout (line-buffered, one line per event).
// Format: DETECT:<watchdog_name>:<detail>
// ---------------------------------------------------------------------------
static void Report(const char* watchdog, const char* detail)
{
    // Prefix with ANSI red in a terminal, but always machine-parseable.
    fprintf(stdout, "DETECT:%s:%s\n", watchdog, detail);
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// CRC-32 (IEEE 802.3 / Ethernet poly)
// ---------------------------------------------------------------------------
static uint32_t Crc32Mem(const uint8_t* p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    while (n--)
    {
        crc ^= *p++;
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}

// ---------------------------------------------------------------------------
// Locate the .text section of this process's own image.
// Returns {base_address, virtual_size} or {0, 0} on failure.
// ---------------------------------------------------------------------------
static std::pair<uintptr_t, DWORD> FindOwnTextSection()
{
    auto* hSelf = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    if (!hSelf) return {0, 0};

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(hSelf);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return {0, 0};

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(hSelf + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return {0, 0};

    const IMAGE_SECTION_HEADER* sect = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sect)
    {
        // Section names are 8 bytes, not null-terminated if exactly 8 chars.
        if (strncmp(reinterpret_cast<const char*>(sect->Name), ".text", 5) == 0)
            return { reinterpret_cast<uintptr_t>(hSelf) + sect->VirtualAddress,
                     sect->Misc.VirtualSize };
    }
    return {0, 0};
}

// ---------------------------------------------------------------------------
// Build a snapshot of all currently-loaded module ranges in this process.
// Used by the page scanner to whitelist known modules.
// ---------------------------------------------------------------------------
static std::vector<std::pair<uintptr_t, uintptr_t>> SnapshotModuleRanges()
{
    std::vector<std::pair<uintptr_t, uintptr_t>> ranges;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                           GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return ranges;

    MODULEENTRY32W me{ sizeof(me) };
    if (Module32FirstW(snap, &me))
    {
        do
        {
            auto base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
            ranges.push_back({ base, base + me.modBaseSize });
        }
        while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return ranges;
}

// ---------------------------------------------------------------------------
// NtQueryInformationThread wrapper — used by the thread watchdog to read
// the true Win32 start address (not the OS wrapper that CreateThread calls).
// ThreadQuerySetWin32StartAddress == 9 (documented in older SDK, stable).
// ---------------------------------------------------------------------------
using fnNtQIT = NTSTATUS(NTAPI*)(HANDLE, THREADINFOCLASS, PVOID, ULONG, PULONG);
static fnNtQIT g_NtQIT = nullptr;

static bool InitNtQIT()
{
    g_NtQIT = reinterpret_cast<fnNtQIT>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    return g_NtQIT != nullptr;
}

// ---------------------------------------------------------------------------
// WATCHDOG 1 — Thread Start-Address Watchdog
//
// Enumerates every thread in this process and queries its Win32 start address.
// If the start address falls in a MEM_PRIVATE (unbacked) region it means the
// thread was created from manually-mapped shellcode that has no module backing.
//
// This is the primary detection used by EAC, BattlEye, and Vanguard against
// all manual-mapping loaders that create visible threads.
// ---------------------------------------------------------------------------
static DWORD WINAPI ThreadWatchdog(LPVOID)
{
    const DWORD selfTid = GetCurrentThreadId();
    const DWORD selfPid = GetCurrentProcessId();

    while (!g_shutdown.load(std::memory_order_relaxed))
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE)
        {
            THREADENTRY32 te{ sizeof(te) };
            if (Thread32First(snap, &te))
            {
                do
                {
                    if (te.th32OwnerProcessID != selfPid) continue;
                    if (te.th32ThreadID      == selfTid)  continue; // skip self

                    HANDLE hT = OpenThread(THREAD_QUERY_INFORMATION, FALSE,
                                           te.th32ThreadID);
                    if (!hT) continue;

                    PVOID startAddr = nullptr;
                    if (g_NtQIT)
                    {
                        g_NtQIT(hT,
                                static_cast<THREADINFOCLASS>(9 /*ThreadQuerySetWin32StartAddress*/),
                                &startAddr, sizeof(startAddr), nullptr);
                    }
                    CloseHandle(hT);

                    if (!startAddr) continue;

                    MEMORY_BASIC_INFORMATION mbi{};
                    if (!VirtualQuery(startAddr, &mbi, sizeof(mbi))) continue;

                    // MEM_PRIVATE + executable = code not backed by any file on disk.
                    if (mbi.Type == MEM_PRIVATE
                        && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ
                                           | PAGE_EXECUTE_READWRITE
                                           | PAGE_EXECUTE_WRITECOPY)))
                    {
                        char msg[128];
                        snprintf(msg, sizeof(msg),
                                 "TID %lu start=0x%p (MEM_PRIVATE / executable)",
                                 static_cast<unsigned long>(te.th32ThreadID),
                                 startAddr);
                        Report("thread_watchdog", msg);
                    }
                }
                while (Thread32Next(snap, &te));
            }
            CloseHandle(snap);
        }

        Sleep(SCAN_INTERVAL_MS);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// WATCHDOG 2 — Stealthy PE Page Scanner
//
// Walks all MEM_COMMIT regions in this process's address space.  For every
// private (unbacked) page, it checks whether the first two bytes are 'MZ'.
// A private page starting with 'MZ' is a PE that was written directly into
// memory rather than loaded through the Windows loader, which is the exact
// fingerprint of a manual-map injection.
//
// We refresh the module list each pass so legitimately late-loaded DLLs
// (e.g., loaded by COM or WinRT) don't generate false positives.
// ---------------------------------------------------------------------------
static DWORD WINAPI PageScanner(LPVOID)
{
    while (!g_shutdown.load(std::memory_order_relaxed))
    {
        auto moduleRanges = SnapshotModuleRanges();

        SYSTEM_INFO si{};
        GetSystemInfo(&si);

        uintptr_t addr = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
        const uintptr_t top = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

        while (addr < top && !g_shutdown.load(std::memory_order_relaxed))
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
                break;

            const uintptr_t regionEnd = addr + mbi.RegionSize;

            if (mbi.State  == MEM_COMMIT
                && mbi.Type == MEM_PRIVATE
                && mbi.RegionSize >= 2
                && !(mbi.Protect & PAGE_GUARD)
                && !(mbi.Protect & PAGE_NOACCESS))
            {
                // Readable?
                if (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE
                                   | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                                   | PAGE_EXECUTE_WRITECOPY | PAGE_WRITECOPY))
                {
                    uint16_t magic = 0;
                    SIZE_T   read  = 0;
                    if (ReadProcessMemory(GetCurrentProcess(),
                                         mbi.BaseAddress, &magic, 2, &read)
                        && read == 2
                        && magic == IMAGE_DOS_SIGNATURE) // 0x5A4D
                    {
                        // Is this inside a known loaded module?
                        const auto base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
                        bool knownModule = false;
                        for (auto& [lo, hi] : moduleRanges)
                            if (base >= lo && base < hi) { knownModule = true; break; }

                        if (!knownModule)
                        {
                            char msg[128];
                            snprintf(msg, sizeof(msg),
                                     "MZ header at 0x%p (MEM_PRIVATE, not a loaded module)",
                                     mbi.BaseAddress);
                            Report("page_scanner", msg);
                        }
                    }
                }
            }

            addr = regionEnd;
        }

        Sleep(SCAN_INTERVAL_MS);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// WATCHDOG 3 — .text Section Integrity Guard
//
// Computes a CRC-32 over the host's .text section at startup, then re-checks
// it every SCAN_INTERVAL_MS.  A mismatch means someone has patched a function
// in this process (inline hook, trampoline, etc.).
//
// Real anti-cheat systems use this to detect detour libraries (MinHook, minhook,
// Microsoft Detours) that the cheat injects to hook game functions.
// ---------------------------------------------------------------------------
static DWORD WINAPI TextIntegrityGuard(LPVOID)
{
    const auto [textBase, textSize] = FindOwnTextSection();
    if (!textBase || !textSize)
    {
        // Can't find section — nothing to monitor.
        return 0;
    }

    const uint32_t baseline = Crc32Mem(reinterpret_cast<const uint8_t*>(textBase), textSize);

    while (!g_shutdown.load(std::memory_order_relaxed))
    {
        Sleep(SCAN_INTERVAL_MS);

        const uint32_t current = Crc32Mem(reinterpret_cast<const uint8_t*>(textBase), textSize);
        if (current != baseline)
        {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     ".text CRC mismatch (baseline=0x%08X current=0x%08X size=%lu bytes)",
                     baseline, current, static_cast<unsigned long>(textSize));
            Report("text_integrity", msg);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// WATCHDOG 4 — String Signature Scanner
//
// Searches all MEM_COMMIT regions for known plaintext strings that indicate
// an injected payload with un-erased .rdata.  Any cheat loader that forgets
// to wipe the string table of the injected DLL will trip this.
//
// The list of signatures is intentionally broad — in production anti-cheat
// this would be backed by a rolling telemetry-driven blocklist.
// ---------------------------------------------------------------------------

// Signatures we consider suspicious when found in private memory.
static const char* const SIGS[] = {
    "SparkyDllSuccess",   // Our own DLL's signal string (proves .rdata not erased)
    "SparkyCore",         // Loader/core identity string
    "SPKY",               // Protocol magic as a C-string
    "ManualMap",          // Debug string from the loader
    nullptr
};

// Scan a single readable region for all known signatures.
static void ScanRegionForStrings(const uint8_t* buf, size_t len,
                                  void* baseAddr)
{
    for (const char* const* pp = SIGS; *pp; ++pp)
    {
        const char* sig    = *pp;
        const size_t sigLen = strlen(sig);
        if (sigLen == 0 || sigLen > len) continue;

        const size_t limit = len - sigLen;
        for (size_t off = 0; off <= limit; ++off)
        {
            if (memcmp(buf + off, sig, sigLen) == 0)
            {
                char msg[192];
                snprintf(msg, sizeof(msg),
                         "String '%s' at 0x%p+0x%zX (un-erased DLL string table?)",
                         sig,
                         baseAddr,
                         off);
                Report("sig_scanner", msg);
                // Report once per signature per region, then move to next sig.
                break;
            }
        }
    }
}

static DWORD WINAPI SigScanner(LPVOID)
{
    while (!g_shutdown.load(std::memory_order_relaxed))
    {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);

        uintptr_t addr = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
        const uintptr_t top = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

        // Working buffer for reading each region (capped at 1 MB per pass).
        static constexpr size_t MAX_READ = 1024 * 1024;
        std::vector<uint8_t> buf;

        while (addr < top && !g_shutdown.load(std::memory_order_relaxed))
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
                break;

            const uintptr_t regionEnd = addr + mbi.RegionSize;

            if (mbi.State  == MEM_COMMIT
                && mbi.Type == MEM_PRIVATE
                && !(mbi.Protect & PAGE_GUARD)
                && !(mbi.Protect & PAGE_NOACCESS)
                && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE
                                   | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                                   | PAGE_EXECUTE_WRITECOPY | PAGE_WRITECOPY)))
            {
                const size_t toRead = (std::min)(mbi.RegionSize, MAX_READ);
                if (buf.size() < toRead) buf.resize(toRead);

                SIZE_T bytesRead = 0;
                if (ReadProcessMemory(GetCurrentProcess(),
                                      mbi.BaseAddress, buf.data(), toRead, &bytesRead)
                    && bytesRead > 0)
                {
                    ScanRegionForStrings(buf.data(), bytesRead, mbi.BaseAddress);
                }
            }

            addr = regionEnd;
        }

        Sleep(SCAN_INTERVAL_MS);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main()
{
    // Line-buffer stdout so the Python harness receives output immediately.
    setvbuf(stdout, nullptr, _IOLBF, 0);

    InitNtQIT();

    // Create the "DLL success" event that the injected DummyDll.dll will set.
    // SECURITY_ATTRIBUTES with NULL DACL so the injected code (any integrity) can set it.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = FALSE;

    SECURITY_DESCRIPTOR sd{};
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE); // NULL DACL = everyone
    sa.lpSecurityDescriptor = &sd;

    HANDLE hSuccess = CreateEventW(&sa, TRUE /*manual-reset*/, FALSE,
                                   L"Global\\SparkyDllSuccess");
    if (!hSuccess)
    {
        // "Global\" requires SeCreateGlobalPrivilege; fall back to local namespace.
        hSuccess = CreateEventW(&sa, TRUE, FALSE, L"SparkyDllSuccess");
    }

    // Announce readiness and PID so IntegrationTest.py can parse them.
    fprintf(stdout, "READY:%lu\n", static_cast<unsigned long>(GetCurrentProcessId()));
    fflush(stdout);

    // Start all four watchdog threads.
    HANDLE threads[4];
    threads[0] = CreateThread(nullptr, 0, ThreadWatchdog,     nullptr, 0, nullptr);
    threads[1] = CreateThread(nullptr, 0, PageScanner,        nullptr, 0, nullptr);
    threads[2] = CreateThread(nullptr, 0, TextIntegrityGuard, nullptr, 0, nullptr);
    threads[3] = CreateThread(nullptr, 0, SigScanner,         nullptr, 0, nullptr);

    // Wait up to 30 seconds for the injected DLL to signal success.
    // The integration test is expected to inject within this window.
    static constexpr DWORD WAIT_MS = 30000;
    DWORD waitResult = hSuccess
        ? WaitForSingleObject(hSuccess, WAIT_MS)
        : WAIT_TIMEOUT;

    // Signal watchdogs to stop and wait for them.
    g_shutdown.store(true, std::memory_order_relaxed);
    WaitForMultipleObjects(4, threads, TRUE, 3000);
    for (HANDLE h : threads) CloseHandle(h);
    if (hSuccess) CloseHandle(hSuccess);

    if (waitResult == WAIT_OBJECT_0)
    {
        fprintf(stdout, "SHUTDOWN\n");
        fflush(stdout);
        return 0; // Injection succeeded and DLL signalled.
    }
    else
    {
        fprintf(stdout, "SHUTDOWN\n");
        fflush(stdout);
        return 1; // Timeout — DLL never signalled.
    }
}
