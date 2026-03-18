// ManualMap.cpp – only show changed parts

#include "ManualMap.h"
#include "Logger.h"
#include <vector>
#include <algorithm>

// Use K32* names for MinGW compatibility
static uintptr_t GetRemoteModuleBase(HANDLE hProc, const wchar_t* moduleName) {
    HMODULE hMods[1024];
    DWORD cbNeeded;
    if (K32EnumProcessModules(hProc, hMods, sizeof(hMods), &cbNeeded)) {  // ← K32EnumProcessModules
        for (unsigned i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
            wchar_t szModName[MAX_PATH];
            if (K32GetModuleFileNameExW(hProc, hMods[i], szModName, MAX_PATH)) {  // ← K32GetModuleFileNameExW
                const wchar_t* baseName = wcsrchr(szModName, L'\\');
                if (baseName) baseName++;
                else baseName = szModName;
                if (_wcsicmp(baseName, moduleName) == 0) {
                    return reinterpret_cast<uintptr_t>(hMods[i]);
                }
            }
        }
    }
    return 0;
}

// In ManualMapDll function – fix the import part
// ...
HMODULE hRemoteDll = reinterpret_cast<HMODULE>(GetRemoteModuleBase(hProcess, std::wstring(dllName.begin(), dllName.end()).c_str()));  // ← cast to HMODULE

// ...

// Fix CreateRemoteThread call – add lpThreadId (nullptr ok)
HANDLE hThread = CreateRemoteThread(
    hProcess,
    nullptr,                        // lpThreadAttributes
    0,                              // dwStackSize (default)
    reinterpret_cast<LPTHREAD_START_ROUTINE>(entryPoint),
    reinterpret_cast<LPVOID>(remoteBase),
    0,                              // dwCreationFlags
    nullptr                         // lpThreadId – ADD THIS
);

if (hThread) {
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    Logger::Log(LogLevel::Info, "DllMain called remotely");
} else {
    Logger::Log(LogLevel::Error, "CreateRemoteThread failed: %lu", GetLastError());
}
