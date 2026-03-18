// Logger.cpp
#include "Logger.h"

#include <cstdarg>
#include <cstdio>
#include <Windows.h>    // OutputDebugStringA

std::mutex Logger::mtx;
std::ofstream Logger::file;
bool Logger::initialized = false;

void Logger::Init(const std::string& logFile) {
    std::lock_guard<std::mutex> lock(mtx);
    if (initialized) return;

    file.open(logFile, std::ios::out | std::ios::app);
    if (!file.is_open()) {
        fprintf(stderr, "[ERR] Failed to open log file: %s\n", logFile.c_str());
    } else {
        fprintf(stdout, "[INF] Log initialized: %s\n", logFile.c_str());
    }

    initialized = true;
}

void Logger::Log(LogLevel level, const char* fmt, ...) {
    char buffer[2048]{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    const char* prefix = "";
    switch (level) {
        case LogLevel::Debug:   prefix = "[DBG]"; break;
        case LogLevel::Info:    prefix = "[INF]"; break;
        case LogLevel::Warning: prefix = "[WRN]"; break;
        case LogLevel::Error:   prefix = "[ERR]"; break;
    }

    std::string message = std::string(prefix) + " " + buffer;

    std::lock_guard<std::mutex> lock(mtx);
    printf("%s\n", message.c_str());

    if (file.is_open()) {
        file << message << "\n";
        file.flush();
    }

    // Also send to Windows debug output (visible in DebugView or VS debugger)
    OutputDebugStringA((message + "\n").c_str());
}

void Logger::LogW(LogLevel level, const wchar_t* fmt, ...) {
    wchar_t wbuffer[2048]{};
    va_list args;
    va_start(args, fmt);
    vswprintf_s(wbuffer, _countof(wbuffer), fmt, args);
    va_end(args);

    // Wide → UTF-8 narrow
    int needed = WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wbuffer, -1, &narrow[0], needed, nullptr, nullptr);

    Log(level, "%s", narrow.c_str());
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(mtx);
    if (file.is_open()) {
        file.flush();
        file.close();
    }
    initialized = false;
}
