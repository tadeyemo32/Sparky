#include <windows.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            MessageBoxA(NULL, "Successfully manually mapped the DLL via SparkyLoader!", "SparkyLoader Success", MB_OK | MB_ICONINFORMATION);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
