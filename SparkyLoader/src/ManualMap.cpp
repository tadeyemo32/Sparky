#include "ManualMap.h"
#include "Logger.h"

#include <TlHelp32.h>
#include <fstream>
#include <cstring>
#include <cstdlib>   // rand, srand

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
        HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
        return reinterpret_cast<Fn>(GetProcAddress(hNt, name));
    }

    NTSTATUS AllocateVirtualMemory(HANDLE hProcess, PVOID* pBase,
                                    SIZE_T* pSize, ULONG type, ULONG protect)
    {
        static auto fn = Resolve<NTSTATUS(NTAPI*)(HANDLE,PVOID*,ULONG_PTR,PSIZE_T,ULONG,ULONG)>(
            "NtAllocateVirtualMemory");
        return fn(hProcess, pBase, 0, pSize, type, protect);
    }

    NTSTATUS WriteVirtualMemory(HANDLE hProcess, PVOID dest,
                                 PVOID src, SIZE_T size, SIZE_T* written)
    {
        static auto fn = Resolve<NTSTATUS(NTAPI*)(HANDLE,PVOID,PVOID,SIZE_T,PSIZE_T)>(
            "NtWriteVirtualMemory");
        return fn(hProcess, dest, src, size, written);
    }

    NTSTATUS ReadVirtualMemory(HANDLE hProcess, PVOID src,
                                PVOID dest, SIZE_T size, SIZE_T* read)
    {
        static auto fn = Resolve<NTSTATUS(NTAPI*)(HANDLE,PVOID,PVOID,SIZE_T,PSIZE_T)>(
            "NtReadVirtualMemory");
        return fn(hProcess, src, dest, size, read);
    }

    NTSTATUS ProtectVirtualMemory(HANDLE hProcess, PVOID* pBase,
                                   SIZE_T* pSize, ULONG newProt, ULONG* oldProt)
    {
        static auto fn = Resolve<NTSTATUS(NTAPI*)(HANDLE,PVOID*,PSIZE_T,ULONG,PULONG)>(
            "NtProtectVirtualMemory");
        return fn(hProcess, pBase, pSize, newProt, oldProt);
    }

    NTSTATUS FreeVirtualMemory(HANDLE hProcess, PVOID* pBase,
                                SIZE_T* pSize, ULONG freeType)
    {
        static auto fn = Resolve<NTSTATUS(NTAPI*)(HANDLE,PVOID*,PSIZE_T,ULONG)>(
            "NtFreeVirtualMemory");
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

    // ---- 3. TLS callbacks ---------------------------------------------
    if (opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size)
    {
        auto pTLS = reinterpret_cast<PIMAGE_TLS_DIRECTORY>(
            base + opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
        auto ppCB = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(pTLS->AddressOfCallBacks);
        while (ppCB && *ppCB) { (*ppCB)(base, DLL_PROCESS_ATTACH, nullptr); ++ppCB; }
    }

    // ---- 4. DllMain ---------------------------------------------------
    if (opt.AddressOfEntryPoint)
    {
        using DllEntry = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
        auto entry = reinterpret_cast<DllEntry>(base + opt.AddressOfEntryPoint);
        if (entry((HINSTANCE)base, DLL_PROCESS_ATTACH, nullptr))
            d->hModule = (HINSTANCE)base;
    }

    // ---- 5. Overwrite PE headers with pseudo-random noise -------------
    // Zeroing is detectable (all-zeros page). Use a PRNG seeded with the
    // image base so the pattern is unique per injection.
    uint32_t rng = (uint32_t)(uintptr_t)base ^ 0xDEADBEEF;
    for (uint32_t i = 0; i < 0x1000 && i < d->ImageSize; ++i)
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; // xorshift32
        base[i] = (uint8_t)rng;
    }
}
static void MappingShellcodeEnd() {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool ResolveNtdllThunks(MappingData& d)
{
    // GetModuleHandleW — no LoadLibrary, just PEB walk
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    if (!hNt) return false;

    d.LdrLoadDll             = (MappingData::fnLdrLoadDll)    GetProcAddress(hNt, "LdrLoadDll");
    d.LdrGetProcedureAddress = (MappingData::fnLdrGetProcAddr)GetProcAddress(hNt, "LdrGetProcedureAddress");
    d.RtlInitUnicodeString   = (MappingData::fnRtlInitUniStr) GetProcAddress(hNt, "RtlInitUnicodeString");
    d.RtlInitAnsiString      = (MappingData::fnRtlInitAnsiStr)GetProcAddress(hNt, "RtlInitAnsiString");

    return d.LdrLoadDll && d.LdrGetProcedureAddress
        && d.RtlInitUnicodeString && d.RtlInitAnsiString;
}

static DWORD FindAlertableThread(HANDLE hProcess)
{
    DWORD  pid  = GetProcessId(hProcess);
    HANDLE hSn  = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSn == INVALID_HANDLE_VALUE) return 0;

    THREADENTRY32 te{ sizeof(te) };
    DWORD tid = 0;
    if (Thread32First(hSn, &te))
        do { if (te.th32OwnerProcessID == pid) { tid = te.th32ThreadID; break; } }
        while (Thread32Next(hSn, &te));
    CloseHandle(hSn);
    return tid;
}

static void ApplySectionProtections(HANDLE hProcess, uintptr_t imageBase,
                                     const std::vector<uint8_t>& raw)
{
    auto pDos  = reinterpret_cast<const PIMAGE_DOS_HEADER>(raw.data());
    auto pNT   = reinterpret_cast<const PIMAGE_NT_HEADERS>(raw.data() + pDos->e_lfanew);
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

    // Generate random XOR key for shellcode obfuscation
    srand(GetTickCount() ^ (DWORD)imageBase);
    uint64_t xorKey = ((uint64_t)(rand() & 0xFFFF) << 48)
                    | ((uint64_t)(rand() & 0xFFFF) << 32)
                    | ((uint64_t)(rand() & 0xFFFF) << 16)
                    |  (uint64_t)(rand() & 0xFFFF);

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
                  bool /*erasePEHeader*/,
                  bool /*callDllMain*/)
{
    if (dllBytes.empty())               { Logger::Log(LogLevel::Error, "ManualMap: empty buffer"); return false; }

    auto pDos = reinterpret_cast<const PIMAGE_DOS_HEADER>(dllBytes.data());
    if (pDos->e_magic != IMAGE_DOS_SIGNATURE) { Logger::Log(LogLevel::Error, "ManualMap: bad DOS sig"); return false; }

    auto pNT = reinterpret_cast<const PIMAGE_NT_HEADERS>(dllBytes.data() + pDos->e_lfanew);
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
    md.ImageBase = imageBase;
    md.ImageSize = opt.SizeOfImage;
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
