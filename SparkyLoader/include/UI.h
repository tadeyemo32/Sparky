#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

// UI state shared between UI thread and injection logic
struct UIState
{
    // Settings (editable in UI)
    char serverHost[128] = "127.0.0.1";
    int  serverPort      = 7777;
    char dllPath[512]    = "SparkyCore.dll";
    char processName[128] = "MarvelRivals-Clone.exe";

    // Status (set by background logic)
    bool processFound    = false;
    bool serverConnected = false;
    bool dllReady        = false;
    bool injected        = false;

    uint32_t targetPid   = 0;

    // Log lines (thread-safe access handled by UI)
    std::vector<std::string> logLines;
    void AddLog(const std::string& msg);
};

// Runs the ImGui + GLFW render loop.
// Returns when the user closes the window.
// onInject is called when the user clicks Inject (runs on UI thread — do heavy work async).
void RunUI(UIState& state, std::function<void()> onConnect,
                           std::function<void()> onInject);
