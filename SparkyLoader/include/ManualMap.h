// ManualMap.h
#pragma once

#include <Windows.h>
#include <psapi.h>          // ← ADD THIS
#include <vector>
#include <string>
#include <cstdint>

bool ManualMapDll(
    HANDLE hProcess,
    const std::vector<uint8_t>& dllBytes,
    const std::wstring& dllPathForLogging = L"",
    bool erasePEHeader = true,
    bool callDllMain = true
);

uintptr_t ManualMapDllFile(   // keep uintptr_t
    HANDLE hProcess,
    const std::wstring& dllPath,
    bool erasePEHeader = true,
    bool callDllMain = true
);
