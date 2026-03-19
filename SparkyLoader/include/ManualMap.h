#pragma once
#include <Windows.h>
#include <winternl.h>
#include <vector>
#include <string>
#include <cstdint>

// ---------------------------------------------------------------------------
// NT API wrappers — resolve directly from ntdll to bypass user-mode hooks
// placed by the game's anti-cheat on the standard Win32 exports.
// ---------------------------------------------------------------------------
namespace Nt
{
    NTSTATUS AllocateVirtualMemory(HANDLE hProcess, PVOID* pBase,
                                    SIZE_T* pSize, ULONG type, ULONG protect);

    NTSTATUS WriteVirtualMemory(HANDLE hProcess, PVOID dest,
                                 PVOID src, SIZE_T size, SIZE_T* written);

    NTSTATUS ProtectVirtualMemory(HANDLE hProcess, PVOID* pBase,
                                   SIZE_T* pSize, ULONG newProt, ULONG* oldProt);

    NTSTATUS FreeVirtualMemory(HANDLE hProcess, PVOID* pBase,
                                SIZE_T* pSize, ULONG freeType);
}

// ---------------------------------------------------------------------------
// Data structure mapped into the target alongside the relocation shellcode.
// Function pointers are resolved in the loader (ntdll is at the same VA
// in every process on the same session).
// ---------------------------------------------------------------------------
struct MappingData
{
    uintptr_t ImageBase;
    uint32_t  ImageSize;
    uint32_t  EraseSeed;     // random 32-bit value for PRNG header fill (RtlGenRandom)
    uint8_t   CallDllMain;   // 1 = invoke DllMain; 0 = skip but still signal success
    uint8_t   ErasePEHeader; // 1 = overwrite headers with PRNG noise; 0 = skip (dev)
    uint8_t   _pad[6];       // align to 8 bytes before function pointers

    using fnLdrLoadDll       = NTSTATUS(NTAPI*)(PWSTR, PULONG, PUNICODE_STRING, PHANDLE);
    using fnLdrGetProcAddr   = NTSTATUS(NTAPI*)(HMODULE, PANSI_STRING, ULONG, PVOID*);
    using fnRtlInitUniStr    = VOID(NTAPI*)(PUNICODE_STRING, PCWSTR);
    using fnRtlInitAnsiStr   = VOID(NTAPI*)(PANSI_STRING, PCSZ);
    // RtlAddFunctionTable registers the injected module's x64 unwind data with
    // the Windows exception dispatcher. Without this, any C++ exception thrown
    // inside the injected DLL cannot unwind through injected frames and will
    // terminate the host process. Resolved from ntdll; nullptr = skip SEH step.
    using fnRtlAddFuncTable  = BOOL(NTAPI*)(PVOID FunctionTable,
                                             DWORD EntryCount,
                                             DWORD64 BaseAddress);

    fnLdrLoadDll       LdrLoadDll;
    fnLdrGetProcAddr   LdrGetProcedureAddress;
    fnRtlInitUniStr    RtlInitUnicodeString;
    fnRtlInitAnsiStr   RtlInitAnsiString;
    fnRtlAddFuncTable  RtlAddFunctionTable; // nullptr = SEH skipped

    HINSTANCE hModule; // written back by shellcode on success
};

// ---------------------------------------------------------------------------
// Inject a DLL into hProcess without calling the hooked LoadLibrary export.
// Pipeline:
//   NtAllocateVirtualMemory -> NtWriteVirtualMemory
//   -> relocations
//   -> IAT fix via LdrLoadDll/LdrGetProcedureAddress (bypasses hooked exports)
//   -> import name string erasure (strips DLL/function names from memory)
//   -> TLS callbacks
//   -> RtlAddFunctionTable (registers x64 SEH unwind data — enables C++ exceptions)
//   -> DllMain via APC (no CreateRemoteThread)
//   -> export name + debug directory erasure
//   -> PE header PRNG overwrite (random noise, not zeros)
// ---------------------------------------------------------------------------
bool ManualMapDll(HANDLE hProcess,
                  const std::vector<uint8_t>& dllBytes,
                  const std::wstring& dllPathForLogging = L"",
                  bool erasePEHeader = true,
                  bool callDllMain   = true);

bool ManualMapDllFile(HANDLE hProcess,
                      const std::wstring& dllPath,
                      bool erasePEHeader = true,
                      bool callDllMain   = true);
