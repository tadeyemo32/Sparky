// main.cpp
#include <Windows.h>
#include <string>
#include <vector>
#include <fstream>

#include "Logger.h"
#include "ManualMap.h"

// Helper: wide string → UTF-8 narrow string (needed for MinGW file paths)
std::string WideToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

std::vector<uint8_t> ReadFileToVector(const std::wstring& wpath) {
    std::string path = WideToUTF8(wpath);

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Logger::Log(LogLevel::Error, "Cannot open DLL file: %s", path.c_str());
        return {};
    }

    auto size = file.tellg();
    if (size <= 0) {
        Logger::Log(LogLevel::Error, "DLL file is empty: %s", path.c_str());
        return {};
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);

    if (!file) {
        Logger::Log(LogLevel::Error, "Failed to read DLL: %s", path.c_str());
        return {};
    }

    return buffer;
}

int wmain(int argc, wchar_t* argv[]) {
    Logger::Init();  // creates SparkyLoader.log in current directory

    if (argc < 3) {
        Logger::Log(LogLevel::Error, "Usage: SparkyLoader.exe <PID> <path_to_dll>");
        Logger::Log(LogLevel::Info, "Example: SparkyLoader.exe 4568 C:\\cheats\\mycheat.dll");
        return 1;
    }

    DWORD pid = 0;
    try {
        pid = std::stoul(argv[1]);
    } catch (...) {
        Logger::Log(LogLevel::Error, "Invalid PID format");
        return 1;
    }

    std::wstring dllPath = argv[2];

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        Logger::Log(LogLevel::Error, "OpenProcess failed (PID %lu): error %lu", pid, GetLastError());
        return 1;
    }

    auto dllBytes = ReadFileToVector(dllPath);
    if (dllBytes.empty()) {
        CloseHandle(hProcess);
        return 1;
    }

    Logger::Log(LogLevel::Info, "Attempting to inject into PID %lu", pid);
    Logger::LogW(LogLevel::Info, L"DLL path: %s", dllPath.c_str());

    bool success = ManualMapDll(
        hProcess,
        dllBytes,
        dllPath,
        true,   // erase PE header
        true    // call DllMain
    );

    CloseHandle(hProcess);

    if (success) {
        Logger::Log(LogLevel::Info, "Injection completed successfully");
    } else {
        Logger::Log(LogLevel::Error, "Injection failed");
    }

    Logger::Shutdown();  // optional but good practice

    return success ? 0 : 1;
}
