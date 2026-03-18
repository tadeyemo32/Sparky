// Logger.h
#pragma once

#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static void Init(const std::string& logFile = "SparkyLoader.log");
    static void Log(LogLevel level, const char* fmt, ...);
    static void LogW(LogLevel level, const wchar_t* fmt, ...);

    // Optional: close file on shutdown (call manually if needed)
    static void Shutdown();

private:
    static std::mutex mtx;
    static std::ofstream file;
    static bool initialized;
};
