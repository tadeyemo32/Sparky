// InjectDirect.cpp — headless CLI wrapper around ManualMapDllFile.
//
// Used exclusively by the integration test suite to inject a DLL into a
// target process without launching the full GUI loader.  The integration
// test starts AntiCheatSim.exe, captures its PID, then calls this tool
// to perform the injection, allowing the test to be driven entirely from
// a Python script.
//
// Usage:
//   InjectDirect.exe <pid> <dll_path> [--no-erase] [--no-dllmain]
//
// Exit codes:
//   0  — injection succeeded
//   1  — injection failed (see stderr for reason)
//   2  — bad arguments

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "ManualMap.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 3)
    {
        fwprintf(stderr,
                 L"Usage: InjectDirect.exe <pid> <dll_path> [--no-erase] [--no-dllmain]\n"
                 L"\n"
                 L"  <pid>         Target process ID (decimal)\n"
                 L"  <dll_path>    Full path to the DLL to inject\n"
                 L"  --no-erase    Skip PE header erasure (useful for debugging)\n"
                 L"  --no-dllmain  Skip calling DllMain after mapping\n");
        return 2;
    }

    const DWORD pid = static_cast<DWORD>(_wtol(argv[1]));
    if (pid == 0)
    {
        fwprintf(stderr, L"[!] Invalid PID: %s\n", argv[1]);
        return 2;
    }

    const std::wstring dllPath = argv[2];

    bool erasePEHeader = true;
    bool callDllMain   = true;

    for (int i = 3; i < argc; ++i)
    {
        if (wcscmp(argv[i], L"--no-erase")   == 0) erasePEHeader = false;
        if (wcscmp(argv[i], L"--no-dllmain") == 0) callDllMain   = false;
    }

    // Verify the DLL file exists before opening the target process.
    {
        DWORD attr = GetFileAttributesW(dllPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES)
        {
            fwprintf(stderr, L"[!] DLL not found: %s\n", dllPath.c_str());
            return 1;
        }
    }

    // Open the target with the privileges ManualMap needs.
    HANDLE hProc = OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ
            | PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION,
        FALSE, pid);

    if (!hProc)
    {
        fwprintf(stderr, L"[!] OpenProcess(%lu) failed: error %lu\n",
                 pid, GetLastError());
        return 1;
    }

    fwprintf(stdout, L"[*] Injecting '%s' into PID %lu  (erase=%s  dllmain=%s)\n",
             dllPath.c_str(), pid,
             erasePEHeader ? L"yes" : L"no",
             callDllMain   ? L"yes" : L"no");
    fflush(stdout);

    const bool ok = ManualMapDllFile(hProc, dllPath, erasePEHeader, callDllMain);
    CloseHandle(hProc);

    if (ok)
    {
        fwprintf(stdout, L"[+] Injection succeeded\n");
        fflush(stdout);
        return 0;
    }
    else
    {
        fwprintf(stderr, L"[-] Injection failed\n");
        return 1;
    }
}
