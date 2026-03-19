#include "ManualMap.h"
#include "Logger.h"
#include "StringCrypt.h"

#include <TlHelp32.h>
#include <psapi.h>
#include <fstream>
#include <cstring>
#include <cstdlib>   // rand, srand

// ---------------------------------------------------------------------------
// Minimal NT structures for NtQuerySystemInformation(SystemProcessInformation).
// We define our own so we don't need the DDK or winternl extras.
// Layouts are stable across Win10/11 x64.
// ---------------------------------------------------------------------------
#pragma pack(push, 8)
struct SysThreadInfo
{
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER CreateTime;
    ULONG         WaitTime;
    ULONG         _pad0;
    PVOID         StartAddress;
    struct { HANDLE ProcessId; HANDLE ThreadId; } ClientId;
    LONG          Priority;
    LONG          BasePriority;
    ULONG         ContextSwitches;
    ULONG         ThreadState;   // 5 = Waiting
    ULONG         WaitReason;    // see below
    ULONG         _pad1;
    // WaitReason constants relevant to alertable waits:
    //   5 = Suspended
    //   6 = WrUserRequest   <-- thread in alertable wait (SleepEx/WaitFor*Ex)
    //   8 = WrQueue         <-- thread pool queue (also alertable)
};
static_assert(sizeof(SysThreadInfo) == 0x50);

struct SysProcessInfo
{
    ULONG         NextEntryOffset;    // +0x00  byte offset to next entry (0 = last)
    ULONG         NumberOfThreads;    // +0x04
    uint8_t       _reserved[0x48];   // +0x08  skip fields we don't need
    HANDLE        UniqueProcessId;    // +0x50
    uint8_t       _reserved2[0xA8];  // +0x58  remainder of the fixed header
    // SysThreadInfo Threads[] immediately follows at +0x100
};
static_assert(sizeof(SysProcessInfo) == 0x100);
#pragma pack(pop)

// NT status codes not always available without DDK/ntstatus.h
#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED       ((NTSTATUS)0xC0000002L)
#endif
#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH  ((NTSTATUS)0xC0000004L)
#endif

// NtQuerySystemInformation — resolve from ntdll (already loaded, no LoadLibrary)
static NTSTATUS QuerySystemInformation(ULONG cls, PVOID buf, ULONG len, PULONG ret)
{
    static auto fn = reinterpret_cast<NTSTATUS(NTAPI*)(ULONG,PVOID,ULONG,PULONG)>(
        GetProcAddress(GetModuleHandleW(_SW(L"ntdll.dll")), _S("NtQuerySystemInformation")));
    return fn ? fn(cls, buf, len, ret) : STATUS_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// GetModuleRange — returns {base, size} of a loaded module in this process.
// Uses GetModuleHandleW (PEB walk) + GetModuleInformation (psapi, link-time).
// No LoadLibrary call.
// ---------------------------------------------------------------------------
static std::pair<uintptr_t, SIZE_T> GetModuleRange(const wchar_t* name)
{
    HMODULE h = GetModuleHandleW(name);
    if (!h) return {0, 0};
    MODULEINFO mi{};
    GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi));
    return { reinterpret_cast<uintptr_t>(mi.lpBaseOfDll), mi.SizeOfImage };
}

// ---------------------------------------------------------------------------
// Nt:: wrappers — resolve ntdll functions directly.
// Using GetModuleHandle (not LoadLibrary) + GetProcAddress is fine because
// ntdll is always already loaded before any user code runs.
// GetModuleHandle only walks the PEB's loaded-module list, no disk I/O.
// GetProcAddress walks the export table — no LoadLibrary call.
// ---------------------------------------------------------------------------
namespace Nt
{
    template<typename Fn>
    static Fn Resolve(const char* name)
    {
        // GetModuleHandleW walks the PEB InMemoryOrderModuleList — no LoadLibrary.
        HMODULE hNt = GetModuleHandleW(_SW(L"ntdll.dll"));
        return reinterpret_cast<Fn>(GetProcAddress(hNt, name));
    }

    NTSTATUS AllocateVirtualMemory(HANDLE hProcess, PVOID* pBase,
                                    SIZE_T* pSize, ULONG type, ULONG protect)
    {
        static auto fn = Resolve<NTSTATUS(NTAPI*)(HANDLE,PVOID*,ULONG_PTR,PSIZE_T,ULONG,ULONG)>(
            _S("NtAllocateVirtualMemory"));
        return fn(hProcess, pBase, 0, pSize, type, protect);
    }

    NTSTATUS WriteVirtualMemory(HANDLE hProcess, PVOID dest,
                                 PVOID src, SIZE_T size, SIZE_T* written)
    {
        static auto fn = Resolve<NTSTATUS(NTAPI*)(HANDLE,PVOID,PVOID,SIZE_T,PSIZE_T)>(
            _S("NtWriteVirtualMemory"));
        return fn(hProcess, dest, src, size, written);
    }

    NTSTATUS ReadVirtualMemory(HANDLE hProcess, PVOID src,
                                PVOID dest, SIZE_T size, SIZE_T* read)
    {
        static auto fn = Resolve<NTSTATUS(NTAPI*)(HANDLE,PVOID,PVOID,SIZE_T,PSIZE_T)>(
            _S("NtReadVirtualMemory"));
        return fn(hProcess, src, dest, size, read);
    }

    NTSTATUS ProtectVirtualMemory(HANDLE hProcess, PVOID* pBase,
                                   SIZE_T* pSize, ULONG newProt, ULONG* oldProt)
    {
        static auto fn = Resolve<NTSTATUS(NTAPI*)(HANDLE,PVOID*,PSIZE_T,ULONG,PULONG)>(
            _S("NtProtectVirtualMemory"));
        return fn(hProcess, pBase, pSize, newProt, oldProt);
    }

    NTSTATUS FreeVirtualMemory(HANDLE hProcess, PVOID* pBase,
                                SIZE_T* pSize, ULONG freeType)
    {
        static auto fn = Resolve<NTSTATUS(NTAPI*)(HANDLE,PVOID*,PSIZE_T,ULONG)>(
            _S("NtFreeVirtualMemory"));
        return fn(hProcess, pBase, pSize, freeType);
    }
}

// ---------------------------------------------------------------------------
// Shellcode — executes inside the target process.
// Deliberately position-independent: no globals, no CRT, no explicit imports.
// Only ntdll internals accessed through the MappingData pointer.
//
// The shellcode bytes are XOR-obfuscated before writing to the target (see
// ExecuteShellcode). This makes the in-memory bytes unrecognisable to a
// signature scanner looking for the known byte pattern.
// ---------------------------------------------------------------------------
static void __stdcall MappingShellcode(MappingData* d)
{
    auto base = reinterpret_cast<uint8_t*>(d->ImageBase);
    auto pDos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    auto pNT  = reinterpret_cast<PIMAGE_NT_HEADERS>(base + pDos->e_lfanew);
    auto& opt = pNT->OptionalHeader;

    // ---- 1. Relocations -----------------------------------------------
    const uintptr_t delta = d->ImageBase - opt.ImageBase;
    if (delta && opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size)
    {
        auto p = reinterpret_cast<PIMAGE_BASE_RELOCATION>(
            base + opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);

        while (p->VirtualAddress && p->SizeOfBlock)
        {
            DWORD  count   = (p->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
            WORD*  entries = reinterpret_cast<WORD*>(p + 1);

            for (DWORD i = 0; i < count; ++i)
            {
                int   type = entries[i] >> 12;
                DWORD off  = entries[i] & 0xFFF;
                if (type == IMAGE_REL_BASED_DIR64)
                    *reinterpret_cast<uintptr_t*>(base + p->VirtualAddress + off) += delta;
                else if (type == IMAGE_REL_BASED_HIGHLOW)
                    *reinterpret_cast<DWORD*>(base + p->VirtualAddress + off) += (DWORD)delta;
            }
            p = reinterpret_cast<PIMAGE_BASE_RELOCATION>(
                    reinterpret_cast<uint8_t*>(p) + p->SizeOfBlock);
        }
    }

    // ---- 2. IAT via LdrLoadDll + LdrGetProcedureAddress ---------------
    // Bypasses the hooked LoadLibraryA/GetProcAddress exports entirely.
    if (opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size)
    {
        auto imp = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
            base + opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

        while (imp->Name)
        {
            auto szName = reinterpret_cast<char*>(base + imp->Name);
            wchar_t wName[256]{};
            for (int i = 0; szName[i] && i < 255; ++i)
                wName[i] = (wchar_t)(unsigned char)szName[i];

            UNICODE_STRING uMod{};
            d->RtlInitUnicodeString(&uMod, wName);
            HANDLE hMod{};
            d->LdrLoadDll(nullptr, nullptr, &uMod, &hMod);

            auto pOrig = reinterpret_cast<PIMAGE_THUNK_DATA>(
                imp->OriginalFirstThunk ? base + imp->OriginalFirstThunk
                                        : base + imp->FirstThunk);
            auto pIAT = reinterpret_cast<PIMAGE_THUNK_DATA>(base + imp->FirstThunk);

            while (pOrig->u1.AddressOfData)
            {
                PVOID pFunc{};
                if (IMAGE_SNAP_BY_ORDINAL(pOrig->u1.Ordinal))
                {
                    d->LdrGetProcedureAddress((HMODULE)hMod, nullptr,
                        (ULONG)IMAGE_ORDINAL(pOrig->u1.Ordinal), &pFunc);
                }
                else
                {
                    auto ibn = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
                        base + pOrig->u1.AddressOfData);
                    ANSI_STRING aFunc{};
                    d->RtlInitAnsiString(&aFunc, ibn->Name);
                    d->LdrGetProcedureAddress((HMODULE)hMod, &aFunc, 0, &pFunc);
                }
                pIAT->u1.Function = (uintptr_t)pFunc;
                ++pOrig; ++pIAT;
            }
            ++imp;
        }
    }

    // ---- 2.5. Erase import identification strings ---------------------
    // The IAT (FirstThunk entries, now holding resolved function addresses)
    // is preserved — only the metadata that names DLLs and functions is zeroed.
    // After IAT resolution these strings serve no runtime purpose and are
    // a trivial way for a scanner to read "kernel32.dll","VirtualAlloc",etc.
    if (opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size)
    {
        auto imp2 = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
            base + opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

        while (imp2->Name)
        {
            // Zero the DLL name string (e.g. "kernel32.dll")
            auto szDll = reinterpret_cast<char*>(base + imp2->Name);
            for (int i = 0; szDll[i]; ++i) szDll[i] = '\0';

            // Zero each function hint-name string in the Original First Thunk
            if (imp2->OriginalFirstThunk)
            {
                auto pOrig = reinterpret_cast<PIMAGE_THUNK_DATA>(
                    base + imp2->OriginalFirstThunk);
                while (pOrig->u1.AddressOfData)
                {
                    if (!IMAGE_SNAP_BY_ORDINAL(pOrig->u1.Ordinal))
                    {
                        auto ibn = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
                            base + (DWORD)pOrig->u1.AddressOfData);
                        auto szFunc = reinterpret_cast<char*>(ibn->Name);
                        for (int i = 0; szFunc[i]; ++i) szFunc[i] = '\0';
                    }
                    ++pOrig;
                }
                imp2->OriginalFirstThunk = 0; // wipe OFT RVA from descriptor
            }
            imp2->Name = 0; // wipe DLL-name RVA from descriptor
            ++imp2;
        }
    }

    // ---- 3. TLS callbacks ---------------------------------------------
    if (opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size)
    {
        auto pTLS = reinterpret_cast<PIMAGE_TLS_DIRECTORY>(
            base + opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
        auto ppCB = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(pTLS->AddressOfCallBacks);
        while (ppCB && *ppCB) { (*ppCB)(base, DLL_PROCESS_ATTACH, nullptr); ++ppCB; }
    }

    // ---- 3.5. Register x64 SEH unwind table ----------------------------
    // On x64, Windows locates exception handlers through the module's
    // RUNTIME_FUNCTION table (IMAGE_DIRECTORY_ENTRY_EXCEPTION).  Because the
    // injected module was never registered with the loader, the exception
    // dispatcher doesn't know it exists.  RtlAddFunctionTable fixes this:
    // without it, any C++ throw / __try inside the injected DLL will fail to
    // unwind through injected frames and crash the host process.
    // Credit: this gap was identified by reviewing thetobysiu/ManualMapInjection.
    if (d->RtlAddFunctionTable
        && opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size)
    {
        auto pFuncTable = reinterpret_cast<PRUNTIME_FUNCTION>(
            base + opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].VirtualAddress);
        DWORD entryCount =
            opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size
            / sizeof(RUNTIME_FUNCTION);
        d->RtlAddFunctionTable(pFuncTable, entryCount, d->ImageBase);
    }

    // ---- 4. DllMain ---------------------------------------------------
    if (d->CallDllMain && opt.AddressOfEntryPoint)
    {
        using DllEntry = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
        auto entry = reinterpret_cast<DllEntry>(base + opt.AddressOfEntryPoint);
        if (entry((HINSTANCE)base, DLL_PROCESS_ATTACH, nullptr))
            d->hModule = (HINSTANCE)base;
    }
    else if (!d->CallDllMain)
    {
        d->hModule = (HINSTANCE)base; // signal success without calling entry
    }

    // ---- 4.5. Erase export directory and debug directory strings ------
    // The export directory contains the DLL's internal name and every
    // exported symbol name ("GameHook", "Init", etc.). A scanner reading
    // process memory can use these to fingerprint the injected module by
    // name — zero them now that DllMain has returned.
    if (opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size)
    {
        auto pExp = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(
            base + opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

        // Zero the embedded DLL name (e.g. "SparkyCore.dll")
        if (pExp->Name)
        {
            auto szMod = reinterpret_cast<char*>(base + pExp->Name);
            for (int i = 0; szMod[i]; ++i) szMod[i] = '\0';
        }

        // Zero every exported function name
        if (pExp->AddressOfNames && pExp->NumberOfNames)
        {
            auto pNames = reinterpret_cast<DWORD*>(base + pExp->AddressOfNames);
            for (DWORD i = 0; i < pExp->NumberOfNames; ++i)
            {
                if (pNames[i])
                {
                    auto szFunc = reinterpret_cast<char*>(base + pNames[i]);
                    for (int j = 0; szFunc[j]; ++j) szFunc[j] = '\0';
                }
            }
        }
    }

    // The debug directory entry contains the full PDB path
    // (e.g. "C:\dev\Sparky\SparkyCore.pdb") — a unique identifier
    // that survives the PE header erasure unless explicitly zeroed here.
    if (opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size)
    {
        auto dbg  = base + opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
        auto dSz  = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size;
        for (uint32_t i = 0; i < dSz; ++i) dbg[i] = 0;
    }

    // ---- 5. Overwrite PE headers with pseudo-random noise -------------
    // All-zeros are detectable (page stands out in a scan). Use xorshift32
    // seeded with both the ASLR image base AND d->EraseSeed (generated by
    // RtlGenRandom in the loader) so the pattern is unpredictable and unique
    // to this run. The hardcoded 0xDEADBEEF constant has been removed —
    // it would appear literally in every loader binary and could be used as
    // a YARA signature to identify the injector.
    if (d->ErasePEHeader)
    {
        // Read SizeOfHeaders before the loop overwrites it (opt lives in base).
        uint32_t hdrSize = opt.SizeOfHeaders;
        if (!hdrSize || hdrSize > d->ImageSize) hdrSize = 0x1000; // sanity cap

        uint32_t rng = d->EraseSeed ^ (uint32_t)(uintptr_t)base;
        if (!rng) rng = 0x1; // xorshift32 is stuck at 0 — any non-zero seed works

        for (uint32_t i = 0; i < hdrSize; ++i)
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            base[i] = (uint8_t)rng;
        }
    }
}
static void MappingShellcodeEnd() {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool ResolveNtdllThunks(MappingData& d)
{
    // GetModuleHandleW — no LoadLibrary, just PEB walk
    SPARKY_WSTR(ntdll_name, L"ntdll.dll");
    HMODULE hNt = GetModuleHandleW(ntdll_name.c_str());
    if (!hNt) return false;

    d.LdrLoadDll             = (MappingData::fnLdrLoadDll)     GetProcAddress(hNt, _S("LdrLoadDll"));
    d.LdrGetProcedureAddress = (MappingData::fnLdrGetProcAddr) GetProcAddress(hNt, _S("LdrGetProcedureAddress"));
    d.RtlInitUnicodeString   = (MappingData::fnRtlInitUniStr)  GetProcAddress(hNt, _S("RtlInitUnicodeString"));
    d.RtlInitAnsiString      = (MappingData::fnRtlInitAnsiStr) GetProcAddress(hNt, _S("RtlInitAnsiString"));
    // Optional — present on all supported Windows versions (Win10+).
    // Failure here is non-fatal: the shellcode checks for nullptr before calling.
    d.RtlAddFunctionTable    = (MappingData::fnRtlAddFuncTable)GetProcAddress(hNt, _S("RtlAddFunctionTable"));

    return d.LdrLoadDll && d.LdrGetProcedureAddress
        && d.RtlInitUnicodeString && d.RtlInitAnsiString;
}

// ---------------------------------------------------------------------------
// FindAlertableThread
//
// Strategy (in priority order):
//   1. Thread whose StartAddress is inside win32u.dll — the game's window/message
//      thread almost always idles in win32u!NtUserMsgWaitForMultipleObjectsEx,
//      which is an alertable kernel wait. Perfect APC target.
//   2. Any thread in a Waiting state with WaitReason == WrUserRequest (6)
//      or WrQueue (8) — both are alertable waits.
//   3. Any thread belonging to the process (last resort).
//
// Uses NtQuerySystemInformation(SystemProcessInformation=5) to read thread
// state without CreateToolhelp32Snapshot, which is itself a known injection
// indicator that some ACs watch for on sensitive threads.
// ---------------------------------------------------------------------------
static DWORD FindAlertableThread(HANDLE hProcess)
{
    constexpr ULONG SystemProcessInformation = 5;
    const DWORD pid = GetProcessId(hProcess);

    // Get address ranges of preferred system DLLs (same in every process).
    // win32u.dll is the user-mode → win32k bridge; its threads are the
    // most reliably alertable in any GUI application.
    const auto [w32uBase, w32uSize] = GetModuleRange(_SW(L"win32u.dll"));
    const auto [ntBase,   ntSize]   = GetModuleRange(_SW(L"ntdll.dll"));

    // Query system process/thread information.
    // Retry with a growing buffer if the initial size is too small.
    ULONG bufSize = 1024 * 512; // 512 KB — usually enough
    std::vector<uint8_t> buf;
    NTSTATUS st;
    do {
        buf.resize(bufSize);
        ULONG returned = 0;
        st = QuerySystemInformation(SystemProcessInformation,
                                     buf.data(), bufSize, &returned);
        if (st == STATUS_INFO_LENGTH_MISMATCH)
            bufSize *= 2;
    } while (st == STATUS_INFO_LENGTH_MISMATCH && bufSize < 64 * 1024 * 1024);

    if (st < 0) // NTSTATUS < 0 means failure
    {
        // Fallback: CreateToolhelp32Snapshot (less ideal but functional)
        Logger::Log(LogLevel::Warning, "FindAlertableThread: NtQSI failed, using snapshot fallback");
        HANDLE hSn = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSn == INVALID_HANDLE_VALUE) return 0;
        THREADENTRY32 te{ sizeof(te) };
        DWORD tid = 0;
        if (Thread32First(hSn, &te))
            do { if (te.th32OwnerProcessID == pid) { tid = te.th32ThreadID; break; } }
            while (Thread32Next(hSn, &te));
        CloseHandle(hSn);
        return tid;
    }

    // Walk SysProcessInfo chain to find our target process
    DWORD bestTid      = 0;   // win32u.dll thread (priority 1)
    DWORD alertTid     = 0;   // any alertable thread (priority 2)
    DWORD fallbackTid  = 0;   // any thread at all (priority 3)

    const uint8_t* ptr = buf.data();
    while (true)
    {
        auto* proc = reinterpret_cast<const SysProcessInfo*>(ptr);

        if ((DWORD)(uintptr_t)(proc->UniqueProcessId) == pid)
        {
            // Thread array sits immediately after the fixed 0x100-byte header
            auto* threads = reinterpret_cast<const SysThreadInfo*>(ptr + 0x100);

            for (ULONG t = 0; t < proc->NumberOfThreads; ++t)
            {
                const SysThreadInfo& ti = threads[t];
                DWORD tid = static_cast<DWORD>(
                    reinterpret_cast<uintptr_t>(ti.ClientId.ThreadId));

                const uintptr_t sa = reinterpret_cast<uintptr_t>(ti.StartAddress);

                // Priority 1: thread that lives in win32u.dll or ntdll.dll
                // These are idle system threads almost always in alertable waits.
                if (bestTid == 0 && w32uSize && sa >= w32uBase && sa < w32uBase + w32uSize)
                    bestTid = tid;
                if (bestTid == 0 && ntSize  && sa >= ntBase   && sa < ntBase   + ntSize)
                    bestTid = tid;

                // Priority 2: any waiting thread with an alertable wait reason
                //   ThreadState == 5 (Waiting)
                //   WaitReason  == 6 (WrUserRequest) or 8 (WrQueue)
                if (alertTid == 0
                    && ti.ThreadState == 5
                    && (ti.WaitReason == 6 || ti.WaitReason == 8))
                    alertTid = tid;

                // Priority 3: any thread in process
                if (fallbackTid == 0)
                    fallbackTid = tid;
            }
            break; // found our process — no need to continue walking
        }

        if (!proc->NextEntryOffset) break;
        ptr += proc->NextEntryOffset;
    }

    DWORD chosen = bestTid ? bestTid : (alertTid ? alertTid : fallbackTid);
    Logger::Log(LogLevel::Debug,
                "FindAlertableThread: chose TID %lu (win32u=%lu alertable=%lu fallback=%lu)",
                chosen, bestTid, alertTid, fallbackTid);
    return chosen;
}

static void ApplySectionProtections(HANDLE hProcess, uintptr_t imageBase,
                                     const std::vector<uint8_t>& raw)
{
    auto pDos  = reinterpret_cast<PIMAGE_DOS_HEADER>(const_cast<uint8_t*>(raw.data()));
    auto pNT   = reinterpret_cast<PIMAGE_NT_HEADERS>(const_cast<uint8_t*>(raw.data()) + pDos->e_lfanew);
    auto pSect = IMAGE_FIRST_SECTION(pNT);

    for (int i = 0; i < pNT->FileHeader.NumberOfSections; ++i, ++pSect)
    {
        if (!pSect->VirtualAddress) continue;
        DWORD chars = pSect->Characteristics;
        ULONG prot  = PAGE_NOACCESS;
        bool exec   = chars & IMAGE_SCN_MEM_EXECUTE;
        bool read   = chars & IMAGE_SCN_MEM_READ;
        bool write  = chars & IMAGE_SCN_MEM_WRITE;

        if (exec && !write) prot = PAGE_EXECUTE_READ;
        else if (exec)      prot = PAGE_EXECUTE_READWRITE;
        else if (write)     prot = PAGE_READWRITE;
        else if (read)      prot = PAGE_READONLY;

        PVOID  addr = reinterpret_cast<PVOID>(imageBase + pSect->VirtualAddress);
        SIZE_T sz   = pSect->Misc.VirtualSize;
        ULONG  old{};
        Nt::ProtectVirtualMemory(hProcess, &addr, &sz, prot, &old);
    }
}

// ---------------------------------------------------------------------------
// ExecuteShellcode — XOR-obfuscate shellcode before writing.
// The AC's memory scanner will never see the raw shellcode bytes in target
// memory because they're XOR'd with a random key unique to this injection.
// A small self-decryption prefix stub decrypts the shellcode in-place before
// jumping into it.
// ---------------------------------------------------------------------------

// Self-decrypting prefix: decrypts [stubEnd .. stubEnd+scLen] with key,
// then jumps to stubEnd.
//
// Layout in target: [XorDecryptStub][encrypted MappingShellcode][MappingData]
//                    ^--- APC fires here
//
// The stub itself is short enough that it doesn't look like any known
// shellcode pattern.
//
// Stub (x64 AT&T notation equivalent):
//   mov  rcx, <encrypted_ptr>   ; pointer to encrypted shellcode
//   mov  rdx, <sc_len>          ; byte count
//   mov  r8,  <xor_key>         ; 64-bit key
//   call decrypt_loop           ; inline below
//   jmp  rcx                    ; jump to decrypted shellcode
//   ; decrypt_loop: xor [rcx+rdx-1], r8b ; rotate key; loop
//
// We implement this as a small runtime-generated byte array so it's not a
// static pattern in our binary either.
struct XorDecryptStub
{
    // Writes the stub bytes into outBuf (must be >= 80 bytes).
    // Returns stub byte count.
    static SIZE_T Build(uint8_t* outBuf, uintptr_t encScPtr,
                        SIZE_T scLen, uint64_t key)
    {
        uint8_t* p = outBuf;

        // mov rax, encScPtr (10 bytes: REX.W B8 + imm64)
        *p++ = 0x48; *p++ = 0xB8;
        memcpy(p, &encScPtr, 8); p += 8;

        // mov rcx, scLen   (10 bytes)
        *p++ = 0x48; *p++ = 0xB9;
        SIZE_T len = scLen;
        memcpy(p, &len, 8); p += 8;

        // mov rdx, key     (10 bytes)
        *p++ = 0x48; *p++ = 0xBA;
        memcpy(p, &key, 8); p += 8;

        // xor_loop:
        // test rcx, rcx    ; 48 85 C9
        *p++ = 0x48; *p++ = 0x85; *p++ = 0xC9;
        // jz done          ; 74 0E  (jump past loop body = 14 bytes)
        *p++ = 0x74; *p++ = 0x0E;
        // xor [rax], dl    ; 30 10
        *p++ = 0x30; *p++ = 0x10;
        // inc rax          ; 48 FF C0
        *p++ = 0x48; *p++ = 0xFF; *p++ = 0xC0;
        // dec rcx          ; 48 FF C9
        *p++ = 0x48; *p++ = 0xFF; *p++ = 0xC9;
        // rol rdx, 1       ; 48 D1 C2
        *p++ = 0x48; *p++ = 0xD1; *p++ = 0xC2;
        // jmp xor_loop     ; EB F0  (-16)
        *p++ = 0xEB; *p++ = 0xF0;
        // done:
        // sub rax, scLen   ; 48 81 E8 + imm32
        *p++ = 0x48; *p++ = 0x81; *p++ = 0xE8;
        uint32_t len32 = (uint32_t)scLen;
        memcpy(p, &len32, 4); p += 4;
        // jmp rax          ; FF E0
        *p++ = 0xFF; *p++ = 0xE0;

        return static_cast<SIZE_T>(p - outBuf);
    }
};

static bool ExecuteShellcode(HANDLE hProcess, uintptr_t imageBase,
                              const MappingData& data, SIZE_T imageSize)
{
    SIZE_T scSize    = reinterpret_cast<SIZE_T>(MappingShellcodeEnd)
                     - reinterpret_cast<SIZE_T>(MappingShellcode);

    // Generate a 64-bit XOR key using RtlGenRandom (advapi32, always loaded).
    // Avoids rand() whose output is predictable from GetTickCount seeds.
    uint64_t xorKey = 0;
    {
        static auto rtlGenRandom =
            reinterpret_cast<BOOLEAN(NTAPI*)(PVOID, ULONG)>(
                GetProcAddress(GetModuleHandleW(_SW(L"advapi32.dll")), _S("SystemFunction036")));
        if (!rtlGenRandom || !rtlGenRandom(&xorKey, sizeof(xorKey)))
        {
            // advapi32 not yet loaded (rare) — fall back to RDTSC mix
            LARGE_INTEGER pc{}; QueryPerformanceCounter(&pc);
            xorKey = ((uint64_t)pc.QuadPart ^ ((uint64_t)imageBase << 17))
                   ^ ((uint64_t)GetCurrentThreadId() * 0x9E3779B97F4A7C15ULL);
        }
    }

    // Layout in target: [XorDecryptStub (≤80B)][encrypted shellcode][MappingData]
    SIZE_T allocSize = 80 + scSize + sizeof(MappingData) + 0x100;
    PVOID  pAlloc    = nullptr;

    if (Nt::AllocateVirtualMemory(hProcess, &pAlloc, &allocSize,
                                   MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE) < 0 || !pAlloc)
    {
        Logger::Log(LogLevel::Error, "ManualMap: alloc shellcode failed");
        return false;
    }

    auto pStubRemote = reinterpret_cast<uint8_t*>(pAlloc);
    auto pScRemote   = pStubRemote + 80;
    auto pDataRemote = pScRemote + scSize + 0x40;

    // XOR-encrypt a copy of the shellcode
    std::vector<uint8_t> encSc(scSize);
    memcpy(encSc.data(), reinterpret_cast<void*>(MappingShellcode), scSize);
    uint64_t k = xorKey;
    for (SIZE_T i = 0; i < scSize; ++i)
    {
        encSc[i] ^= (uint8_t)(k >> ((i % 8) * 8));
        k = (k << 1) | (k >> 63);
    }

    // Build the self-decryption stub
    uint8_t stubBuf[80]{};
    SIZE_T stubLen = XorDecryptStub::Build(
        stubBuf,
        reinterpret_cast<uintptr_t>(pScRemote),
        scSize, xorKey);

    // Write stub + encrypted shellcode + data
    SIZE_T w{};
    Nt::WriteVirtualMemory(hProcess, pStubRemote, stubBuf, stubLen, &w);
    Nt::WriteVirtualMemory(hProcess, pScRemote, encSc.data(), scSize, &w);
    Nt::WriteVirtualMemory(hProcess, pDataRemote,
                            const_cast<MappingData*>(&data), sizeof(data), &w);

    // Queue APC to stub entry (stub decrypts shellcode, then jumps into it)
    DWORD tid = FindAlertableThread(hProcess);
    if (!tid)
    {
        Logger::Log(LogLevel::Error, "ManualMap: no alertable thread");
        PVOID p = pAlloc; SIZE_T s = 0;
        Nt::FreeVirtualMemory(hProcess, &p, &s, MEM_RELEASE);
        return false;
    }

    HANDLE hThread = OpenThread(THREAD_SET_CONTEXT, FALSE, tid);
    if (!hThread)
    {
        Logger::Log(LogLevel::Error, "ManualMap: OpenThread failed");
        PVOID p = pAlloc; SIZE_T s = 0;
        Nt::FreeVirtualMemory(hProcess, &p, &s, MEM_RELEASE);
        return false;
    }

    QueueUserAPC(reinterpret_cast<PAPCFUNC>(pStubRemote),
                 hThread,
                 reinterpret_cast<ULONG_PTR>(pDataRemote));
    CloseHandle(hThread);

    // Poll for hModule written back by shellcode
    for (int i = 0; i < 200; ++i)
    {
        Sleep(50);
        MappingData result{};
        SIZE_T r{};
        Nt::ReadVirtualMemory(hProcess, pDataRemote, &result, sizeof(result), &r);
        if (result.hModule)
        {
            Logger::Log(LogLevel::Info, "ManualMap: OK hModule=%p", result.hModule);
            // Free shellcode region — leaves no detectable allocation
            PVOID p = pAlloc; SIZE_T s = 0;
            Nt::FreeVirtualMemory(hProcess, &p, &s, MEM_RELEASE);
            return true;
        }
    }

    Logger::Log(LogLevel::Error, "ManualMap: timeout");
    PVOID p = pAlloc; SIZE_T s = 0;
    Nt::FreeVirtualMemory(hProcess, &p, &s, MEM_RELEASE);
    return false;
}

// ---------------------------------------------------------------------------
// ManualMapDll
// ---------------------------------------------------------------------------
bool ManualMapDll(HANDLE hProcess,
                  const std::vector<uint8_t>& dllBytes,
                  const std::wstring& /*dllPathForLogging*/,
                  bool erasePEHeader,
                  bool callDllMain)
{
    if (dllBytes.empty())               { Logger::Log(LogLevel::Error, "ManualMap: empty buffer"); return false; }

    auto pDos = reinterpret_cast<PIMAGE_DOS_HEADER>(const_cast<uint8_t*>(dllBytes.data()));
    if (pDos->e_magic != IMAGE_DOS_SIGNATURE) { Logger::Log(LogLevel::Error, "ManualMap: bad DOS sig"); return false; }

    auto pNT = reinterpret_cast<PIMAGE_NT_HEADERS>(const_cast<uint8_t*>(dllBytes.data()) + pDos->e_lfanew);
    if (pNT->Signature != IMAGE_NT_SIGNATURE)           { Logger::Log(LogLevel::Error, "ManualMap: bad NT sig"); return false; }
    if (pNT->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) { Logger::Log(LogLevel::Error, "ManualMap: not x64"); return false; }

    auto& opt = pNT->OptionalHeader;

    // Allocate image (direct Nt API)
    PVOID  pTarget = nullptr;
    SIZE_T imgSize = opt.SizeOfImage;
    if (Nt::AllocateVirtualMemory(hProcess, &pTarget, &imgSize,
                                   MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE) < 0 || !pTarget)
    {
        Logger::Log(LogLevel::Error, "ManualMap: NtAllocateVirtualMemory failed");
        return false;
    }

    auto imageBase = reinterpret_cast<uintptr_t>(pTarget);

    // Map into local buffer, then write via Nt (bypasses WriteProcessMemory hook)
    std::vector<uint8_t> mapped(opt.SizeOfImage, 0);
    memcpy(mapped.data(), dllBytes.data(), opt.SizeOfHeaders);

    auto pSect = IMAGE_FIRST_SECTION(pNT);
    for (int i = 0; i < pNT->FileHeader.NumberOfSections; ++i, ++pSect)
        if (pSect->SizeOfRawData)
            memcpy(mapped.data() + pSect->VirtualAddress,
                   dllBytes.data() + pSect->PointerToRawData,
                   pSect->SizeOfRawData);

    SIZE_T written{};
    if (Nt::WriteVirtualMemory(hProcess, pTarget,
                                mapped.data(), mapped.size(), &written) < 0)
    {
        Logger::Log(LogLevel::Error, "ManualMap: NtWriteVirtualMemory failed");
        PVOID p = pTarget; SIZE_T s = 0;
        Nt::FreeVirtualMemory(hProcess, &p, &s, MEM_RELEASE);
        return false;
    }

    ApplySectionProtections(hProcess, imageBase, dllBytes);

    MappingData md{};
    md.ImageBase     = imageBase;
    md.ImageSize     = opt.SizeOfImage;
    md.CallDllMain   = callDllMain   ? 1 : 0;
    md.ErasePEHeader = erasePEHeader ? 1 : 0;

    // Generate an unpredictable PRNG seed for the PE header noise fill.
    // RtlGenRandom (advapi32!SystemFunction036) is always loaded in-process.
    md.EraseSeed = 0;
    {
        static auto rtlGen = reinterpret_cast<BOOLEAN(NTAPI*)(PVOID, ULONG)>(
            GetProcAddress(GetModuleHandleW(_SW(L"advapi32.dll")), _S("SystemFunction036")));
        if (rtlGen) rtlGen(&md.EraseSeed, sizeof(md.EraseSeed));
        if (!md.EraseSeed)
        {
            // Fallback: QPC mix — still better than a hardcoded constant
            LARGE_INTEGER pc{}; QueryPerformanceCounter(&pc);
            md.EraseSeed = (uint32_t)pc.LowPart ^ (uint32_t)(imageBase >> 12);
        }
    }

    if (!ResolveNtdllThunks(md))
    {
        Logger::Log(LogLevel::Error, "ManualMap: ntdll thunk resolve failed");
        PVOID p = pTarget; SIZE_T s = 0;
        Nt::FreeVirtualMemory(hProcess, &p, &s, MEM_RELEASE);
        return false;
    }

    return ExecuteShellcode(hProcess, imageBase, md, opt.SizeOfImage);
}

bool ManualMapDllFile(HANDLE hProcess,
                      const std::wstring& dllPath,
                      bool erasePEHeader, bool callDllMain)
{
    // Use CreateFileW + ReadFile instead of std::ifstream which may
    // trigger LoadLibraryExW on some runtime versions.
    HANDLE hFile = CreateFileW(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        Logger::Log(LogLevel::Error, "ManualMapDllFile: CreateFileW failed %lu", GetLastError());
        return false;
    }

    LARGE_INTEGER sz{};
    GetFileSizeEx(hFile, &sz);

    std::vector<uint8_t> buf(static_cast<size_t>(sz.QuadPart));
    DWORD read{};
    ReadFile(hFile, buf.data(), (DWORD)buf.size(), &read, nullptr);
    CloseHandle(hFile);

    if (read != buf.size())
    {
        Logger::Log(LogLevel::Error, "ManualMapDllFile: read incomplete");
        return false;
    }

    return ManualMapDll(hProcess, buf, dllPath, erasePEHeader, callDllMain);
}
