#pragma once
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <cstdint>

// ---------------------------------------------------------------------------
// UIState — shared between the render thread and all background worker threads.
//
// Fields the UI reads (background writes):
//   processFound / targetPid  — updated by ProcessWatcher every second
//   serverConnected           — set true after AuthOk
//   dllReady                  — set true once DLL bytes are in RAM or on disk
//   injected                  — set true after ManualMap succeeds
//   connecting / injecting    — spinners / button disabling
//   downloadProgress          — 0..1 progress bar during chunk transfer
//   dllSizeBytes              — displayed next to "Module | Ready"
//
// Fields the UI writes (background reads):
//   serverHost / serverPort / useTls — read once when Connect is clicked
//   processName                      — read by ProcessWatcher each poll
//   dllPath                          — fallback local DLL path (dev mode)
// ---------------------------------------------------------------------------
struct UIState
{
    // ── Editable in UI ──────────────────────────────────────────────────
    char serverHost[128]  = {0};
    int  serverPort       = 443;
    bool useTls           = true;

    char processName[128] = "game.exe";
    char dllPath[512]     = "";         // dev-mode fallback; leave blank for server-only

    // ── Status (written by background threads, read by render) ──────────
    bool     processFound    = false;
    bool     serverConnected = false;
    bool     dllReady        = false;
    bool     injected        = false;

    bool     connecting      = false;   // true while ConnectAndFetchDll is running
    bool     injecting       = false;   // true while ManualMapDll is running

    float    downloadProgress = 0.f;    // 0..1 during chunk stream
    uint32_t dllSizeBytes     = 0;      // populated when dllReady becomes true
    uint32_t targetPid        = 0;

    // ── Log (written from any thread via AddLog; rendered by UI thread) ─
    std::vector<std::string> logLines;
    void AddLog(const std::string& msg);
};

// ---------------------------------------------------------------------------
// RunUI — starts the GLFW + ImGui event loop.  Blocks until the window closes.
//
// onConnect — called on a NEW background thread when the user clicks Connect.
// onInject  — called on a NEW background thread when the user clicks Inject.
// ---------------------------------------------------------------------------
void RunUI(UIState& state,
           std::function<void()> onConnect,
           std::function<void()> onInject);
