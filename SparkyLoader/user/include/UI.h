#pragma once
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <cstdint>

// ---------------------------------------------------------------------------
// ProcessEntry — one row in the Tab-2 process list.
// ---------------------------------------------------------------------------
struct ProcessEntry
{
    uint32_t pid;
    char     name[260];   // UTF-8, MAX_PATH
};

// ---------------------------------------------------------------------------
// UIState — shared between the render thread and all background worker threads.
//
// Background threads WRITE:
//   processFound / targetPid   — ProcessWatcher
//   serverConnected / loggedIn — set after AuthOk
//   dllReady / dllSizeBytes    — set after chunk transfer
//   injected                   — set after ManualMap succeeds
//   connecting / injecting     — spinner / button disable
//   downloadProgress           — 0..1 progress bar
//   loginError                 — non-empty if login failed
//
// UI thread WRITES (background reads):
//   serverHost / serverPort / useTls  — connection config (never shown)
//   processName                       — read by ProcessWatcher each poll
//   username / licenseKey / password  — read once in onConnect, password cleared after
//   selectedPreset                    — which DLL to request
//   customTargetPid                   — non-zero = custom target for Tab-2 inject
// ---------------------------------------------------------------------------
struct UIState
{
    // ── Server config (set in wWinMain; never displayed) ─────────────────
    char serverHost[128] = {0};
    int  serverPort      = 443;
    bool useTls          = true;

    // ── Process watcher ───────────────────────────────────────────────────
    char processName[128] = "game.exe";
    char dllPath[512]     = "";          // dev-mode local fallback

    // ── Login screen ──────────────────────────────────────────────────────
    bool loggedIn        = false;
    bool loggingIn       = false;        // spinner while server responds
    bool signUpMode      = false;        // false = Login tab, true = Sign Up tab
    char username[32]    = {};           // max 31 chars + null
    char licenseKey[40]  = {};           // license key string + null
    char password[128]   = {};           // plaintext; zeroed after hashing
    char passwordConf[128]= {};          // sign-up only: confirm password field
    char loginError[256] = {};           // non-empty → shown as error on login screen

    // ── Status (background → UI) ──────────────────────────────────────────
    bool     processFound     = false;
    bool     serverConnected  = false;
    bool     dllReady         = false;
    bool     injected         = false;
    bool     connecting       = false;
    bool     injecting        = false;
    float    downloadProgress = 0.f;
    uint32_t dllSizeBytes     = 0;
    uint32_t targetPid        = 0;

    // ── Preset selection  (Tab 1 — "Select") ──────────────────────────────
    int  selectedPreset    = -1;    // 0-4; -1 = none
    bool autoInjectPending = false;

    // ── Custom inject  (Tab 2 — "Inject") ─────────────────────────────────
    std::vector<ProcessEntry> processList;
    int      selectedProcessIdx = -1;
    int      selectedCustomDll  = 0;
    uint32_t customTargetPid    = 0;

    // ── Log (background writes; not rendered) ─────────────────────────────
    std::vector<std::string> logLines;
    void AddLog(const std::string& msg);
};

// ---------------------------------------------------------------------------
// RunUI — starts GLFW + ImGui event loop.  Blocks until window closes.
//
// onConnect — called on a NEW background thread when the user logs in or
//             when a preset card is clicked.  Reads username/licenseKey/
//             password for login; reads selectedPreset for DLL fetch.
//             Sets state.loggedIn / state.loginError as appropriate.
//
// onInject  — called on a NEW background thread when the auto-inject
//             condition fires (dllReady && processReady && autoInjectPending).
// ---------------------------------------------------------------------------
void RunUI(UIState& state,
           std::function<void()> onConnect,
           std::function<void()> onInject);
