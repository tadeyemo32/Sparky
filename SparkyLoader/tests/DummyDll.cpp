#include <windows.h>

// DummyDll — injected by the integration test suite into AntiCheatSim.exe.
//
// On DLL_PROCESS_ATTACH it signals the named event "Global\SparkyDllSuccess"
// (or the local-namespace fallback "SparkyDllSuccess") so that:
//   - AntiCheatSim.exe can exit cleanly with code 0, and
//   - IntegrationTest.py can confirm the DLL actually ran.
//
// No MessageBox, no CreateThread, no visible side-effects — this keeps the
// integration test silent and scriptable, and avoids creating an extra thread
// that the AntiCheatSim thread watchdog might otherwise flag.

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        // Try global namespace first (requires SeCreateGlobalPrivilege or
        // a NULL-DACL object created by the target process).
        HANDLE hEv = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Global\\SparkyDllSuccess");
        if (!hEv)
            hEv = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"SparkyDllSuccess");

        if (hEv)
        {
            SetEvent(hEv);
            CloseHandle(hEv);
        }
    }
    return TRUE;
}
