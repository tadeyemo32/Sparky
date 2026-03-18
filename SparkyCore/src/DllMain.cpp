#include <Windows.h>
#include <thread>

#include "SDK/UE5/GObjects.h"

// ---------------------------------------------------------------------------
// Forward declarations — implement in their own files as you build out the
// feature layer. This entry point just initialises the SDK and kicks off
// the main loop on a background thread.
// ---------------------------------------------------------------------------
namespace Core
{
    void Load();
    void Loop();
    void Unload();
}

static HMODULE g_hModule = nullptr;

static DWORD WINAPI MainThread(LPVOID)
{
    Core::Load();
    Core::Loop();
    Core::Unload();
    FreeLibraryAndExitThread(g_hModule, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hInst);
        g_hModule = hInst;

        if (HANDLE h = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr))
            CloseHandle(h);
    }
    return TRUE;
}
