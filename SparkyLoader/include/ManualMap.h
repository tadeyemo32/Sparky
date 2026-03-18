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

    using fnLdrLoadDll       = NTSTATUS(NTAPI*)(PWSTR, PULONG, PUNICODE_STRING, PHANDLE);
    using fnLdrGetProcAddr   = NTSTATUS(NTAPI*)(HMODULE, PANSI_STRING, ULONG, PVOID*);
    using fnRtlInitUniStr    = VOID(NTAPI*)(PUNICODE_STRING, PCWSTR);
    using fnRtlInitAnsiStr   = VOID(NTAPI*)(PANSI_STRING, PCSZ);

    fnLdrLoadDll     LdrLoadDll;
    fnLdrGetProcAddr LdrGetProcedureAddress;
    fnRtlInitUniStr  RtlInitUnicodeString;
    fnRtlInitAnsiStr RtlInitAnsiString;

    HINSTANCE hModule; // written back by shellcode on success
};

// ---------------------------------------------------------------------------
// Inject a DLL into hProcess without calling the hooked LoadLibrary export.
// Pipeline:
//   NtAllocateVirtualMemory -> NtWriteVirtualMemory
//   -> relocation + IAT fix via LdrLoadDll/LdrGetProcedureAddress
//   -> DllMain via APC (no CreateRemoteThread)
//   -> PE header erasure
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
