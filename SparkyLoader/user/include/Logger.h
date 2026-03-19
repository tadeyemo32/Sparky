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
    static void Shutdown();

#ifdef NDEBUG
    // -----------------------------------------------------------------------
    // Release build: all Logger calls compile away to zero instructions.
    // The format-string literals are never referenced and are eliminated by
    // the linker (no .rdata / string-table entry remains for any log message).
    // -----------------------------------------------------------------------
    static void Log (LogLevel, const char*,    ...) noexcept {}
    static void LogW(LogLevel, const wchar_t*, ...) noexcept {}
#else
    static void Log (LogLevel level, const char*    fmt, ...);
    static void LogW(LogLevel level, const wchar_t* fmt, ...);
#endif

private:
    static std::mutex    mtx;
    static std::ofstream file;
    static bool          initialized;
};
