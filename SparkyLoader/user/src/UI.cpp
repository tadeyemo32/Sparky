#include "UI.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>

#include <mutex>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstring>
#include <cstdio>

static std::mutex g_logMutex;

// ---------------------------------------------------------------------------
// UIState::AddLog
// ---------------------------------------------------------------------------
void UIState::AddLog(const std::string& msg)
{
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char ts[12]{};
    struct tm tmb{};
#ifdef _WIN32
    localtime_s(&tmb, &t);
#else
    localtime_r(&t, &tmb);
#endif
    snprintf(ts, sizeof(ts), "%02d:%02d:%02d",
             tmb.tm_hour, tmb.tm_min, tmb.tm_sec);

    std::lock_guard lk(g_logMutex);
    logLines.push_back(std::string("[") + ts + "] " + msg);
    if (logLines.size() > 300)
        logLines.erase(logLines.begin());
}

// ---------------------------------------------------------------------------
// Preset definitions
// ---------------------------------------------------------------------------
struct PresetInfo { const char* name; const char* desc; const char* icon; };

static constexpr PresetInfo kPresets[5] = {
    { "AIMBOT",  "Precision targeting module",  "\xE2\x98\x85" },  // ★
    { "VISUALS", "ESP & overlay module",         "\xE2\x97\x8E" },  // ◎
    { "ESP",     "Extended situational panel",   "\xE2\x8C\x96" },  // ⌖
    { "MISC",    "Utility & speed mods",         "\xE2\x9A\x99" },  // ⚙
    { "FULL",    "Complete feature bundle",      "\xE2\x9A\xA1" },  // ⚡
};

// ---------------------------------------------------------------------------
// Colour palette
// ---------------------------------------------------------------------------
namespace Col
{
    static constexpr ImVec4 Bg          = {0.04f,  0.04f,  0.08f,  1.f};
    static constexpr ImVec4 BgDeep      = {0.02f,  0.02f,  0.05f,  1.f};
    static constexpr ImVec4 BgPanel     = {0.06f,  0.06f,  0.12f,  1.f};
    static constexpr ImVec4 BgCard      = {0.06f,  0.07f,  0.14f,  1.f};
    static constexpr ImVec4 BgCardSel   = {0.04f,  0.22f,  0.34f,  1.f};
    static constexpr ImVec4 BgCardHov   = {0.08f,  0.12f,  0.24f,  1.f};
    static constexpr ImVec4 BgInput     = {0.07f,  0.08f,  0.16f,  1.f};

    static constexpr ImVec4 Border      = {0.15f,  0.18f,  0.32f,  1.f};
    static constexpr ImVec4 BorderDim   = {0.10f,  0.12f,  0.22f,  0.6f};
    static constexpr ImVec4 BorderGlow  = {0.00f,  0.60f,  0.90f,  0.5f};

    static constexpr ImVec4 Accent      = {0.00f,  0.78f,  1.00f,  1.f};
    static constexpr ImVec4 AccentDim   = {0.00f,  0.52f,  0.70f,  1.f};
    static constexpr ImVec4 AccentDeep  = {0.00f,  0.30f,  0.42f,  1.f};

    static constexpr ImVec4 Green       = {0.12f,  0.88f,  0.50f,  1.f};
    static constexpr ImVec4 GreenDim    = {0.06f,  0.55f,  0.30f,  1.f};
    static constexpr ImVec4 Red         = {0.92f,  0.26f,  0.26f,  1.f};
    static constexpr ImVec4 Orange      = {1.00f,  0.65f,  0.12f,  1.f};

    static constexpr ImVec4 Discord     = {0.35f,  0.40f,  0.95f,  1.f};
    static constexpr ImVec4 DiscordHov  = {0.45f,  0.50f,  1.00f,  1.f};

    static constexpr ImVec4 TextMain    = {0.90f,  0.90f,  0.95f,  1.f};
    static constexpr ImVec4 TextDim     = {0.45f,  0.47f,  0.62f,  1.f};
    static constexpr ImVec4 TextLabel   = {0.56f,  0.60f,  0.78f,  1.f};
    static constexpr ImVec4 TextDisable = {0.28f,  0.28f,  0.38f,  1.f};
}

static ImU32 U32(const ImVec4& v)
{
    return ImGui::ColorConvertFloat4ToU32(v);
}

static ImVec4 Lerp4(const ImVec4& a, const ImVec4& b, float t)
{
    return { a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t, a.w+(b.w-a.w)*t };
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
static void ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding      = {18.f, 18.f};
    s.FramePadding       = {11.f,  7.f};
    s.CellPadding        = { 8.f,  4.f};
    s.ItemSpacing        = { 8.f,  7.f};
    s.ItemInnerSpacing   = { 6.f,  4.f};
    s.IndentSpacing      = 18.f;
    s.ScrollbarSize      = 9.f;
    s.GrabMinSize        = 10.f;
    s.WindowBorderSize   = 1.f;
    s.FrameBorderSize    = 1.f;
    s.PopupBorderSize    = 1.f;
    s.TabBorderSize      = 0.f;
    s.WindowRounding     = 12.f;
    s.ChildRounding      = 8.f;
    s.FrameRounding      = 7.f;
    s.ScrollbarRounding  = 5.f;
    s.GrabRounding       = 5.f;
    s.TabRounding        = 7.f;
    s.PopupRounding      = 9.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = Col::Bg;
    c[ImGuiCol_PopupBg]              = Col::BgPanel;
    c[ImGuiCol_ChildBg]              = {0.05f, 0.05f, 0.10f, 1.f};
    c[ImGuiCol_Border]               = Col::Border;
    c[ImGuiCol_BorderShadow]         = {0.f, 0.f, 0.f, 0.f};
    c[ImGuiCol_TitleBg]              = Col::BgDeep;
    c[ImGuiCol_TitleBgActive]        = Col::BgDeep;
    c[ImGuiCol_TitleBgCollapsed]     = Col::BgDeep;
    c[ImGuiCol_Text]                 = Col::TextMain;
    c[ImGuiCol_TextDisabled]         = Col::TextDisable;
    c[ImGuiCol_FrameBg]              = Col::BgInput;
    c[ImGuiCol_FrameBgHovered]       = {0.10f, 0.12f, 0.22f, 1.f};
    c[ImGuiCol_FrameBgActive]        = {0.13f, 0.16f, 0.28f, 1.f};
    c[ImGuiCol_CheckMark]            = Col::Accent;
    c[ImGuiCol_Button]               = {0.08f, 0.10f, 0.20f, 1.f};
    c[ImGuiCol_ButtonHovered]        = Col::AccentDeep;
    c[ImGuiCol_ButtonActive]         = Col::AccentDim;
    c[ImGuiCol_Header]               = {0.08f, 0.10f, 0.20f, 1.f};
    c[ImGuiCol_HeaderHovered]        = {0.10f, 0.14f, 0.26f, 1.f};
    c[ImGuiCol_HeaderActive]         = Col::AccentDim;
    c[ImGuiCol_Separator]            = Col::BorderDim;
    c[ImGuiCol_SeparatorHovered]     = Col::AccentDim;
    c[ImGuiCol_SeparatorActive]      = Col::Accent;
    c[ImGuiCol_ResizeGrip]           = {0.f, 0.f, 0.f, 0.f};
    c[ImGuiCol_ScrollbarBg]          = Col::BgDeep;
    c[ImGuiCol_ScrollbarGrab]        = Col::Border;
    c[ImGuiCol_ScrollbarGrabHovered] = Col::AccentDim;
    c[ImGuiCol_ScrollbarGrabActive]  = Col::Accent;
    c[ImGuiCol_Tab]                  = {0.05f, 0.07f, 0.14f, 1.f};
    c[ImGuiCol_TabHovered]           = Col::AccentDeep;
    c[ImGuiCol_TabActive]            = {0.04f, 0.22f, 0.34f, 1.f};
    c[ImGuiCol_TabUnfocused]         = {0.04f, 0.06f, 0.12f, 1.f};
    c[ImGuiCol_TabUnfocusedActive]   = {0.04f, 0.18f, 0.28f, 1.f};
    c[ImGuiCol_PlotHistogram]        = Col::AccentDim;
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------
static void AccentSep()
{
    ImGui::PushStyleColor(ImGuiCol_Separator, Col::AccentDim);
    ImGui::Separator();
    ImGui::PopStyleColor();
}

static const char* Spinner()
{
    static const char* f[] = {"◐","◓","◑","◒"};
    return f[(int)(ImGui::GetTime() * 7.0) & 3];
}

static void FormatBytes(char* buf, size_t n, uint32_t bytes)
{
    if (bytes >= 1024u*1024u) snprintf(buf, n, "%.1f MB", bytes/(1024.f*1024.f));
    else if (bytes >= 1024u)  snprintf(buf, n, "%.1f KB", bytes/1024.f);
    else                      snprintf(buf, n, "%u B", bytes);
}

// Pulsing animation value (0..1..0) driven by ImGui time
static float PulseValue(float speed = 2.f)
{
    return 0.5f + 0.5f * std::sinf((float)ImGui::GetTime() * speed);
}

// Status dot + label on one line
static void StatusRow(const char* label, bool active, bool pending,
                      const char* activeText, const char* pendingText, const char* offText)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p  = ImGui::GetCursorScreenPos();
    float cx  = p.x + 7.f;
    float cy  = p.y + 9.f;

    ImVec4 dotCol = active  ? Col::Green :
                    pending ? Col::Orange : Col::Red;

    if (active || pending)
    {
        float pulse = active ? 0.15f : PulseValue(3.f) * 0.25f;
        dl->AddCircleFilled({cx, cy}, 9.f, U32({dotCol.x, dotCol.y, dotCol.z, pulse}));
    }
    dl->AddCircleFilled({cx, cy}, 4.f, U32(dotCol));

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.f);
    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextLabel);
    ImGui::Text("%-10s", label);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.f, 6.f);
    ImGui::PushStyleColor(ImGuiCol_Text, active ? Col::TextMain : pending ? Col::Orange : Col::TextDim);
    ImGui::TextUnformatted(active ? activeText : pending ? pendingText : offText);
    ImGui::PopStyleColor();
}

// Small connection status dot used in the top-right corner chrome
static void DrawStatusDot(ImDrawList* dl, ImVec2 center, bool connected, bool pending)
{
    // pending (logging in / connecting) takes priority so the dot pulses orange
    // while authenticating; turns green as soon as the server is reachable.
    ImVec4 col = pending ? Col::Orange : connected ? Col::Green : Col::Red;
    if (pending)
        dl->AddCircleFilled(center, 9.f, U32({col.x, col.y, col.z, PulseValue(4.f) * 0.22f}));
    else if (connected)
        dl->AddCircleFilled(center, 9.f, U32({col.x, col.y, col.z, 0.12f}));
    dl->AddCircleFilled(center, 4.f, U32(col));
}

// Discord icon button (bottom-right) — returns true if clicked
static bool DiscordButton(ImVec2 windowBR)
{
    const float sz   = 26.f;
    const float pad  = 10.f;
    ImVec2 btnPos    = { windowBR.x - sz - pad, windowBR.y - sz - pad };

    ImGui::SetCursorPos({ ImGui::GetWindowWidth()  - sz - pad,
                          ImGui::GetWindowHeight() - sz - pad });
    ImGui::PushID("##discord");
    ImGui::InvisibleButton("##dc", {sz, sz});
    bool hov     = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 abs = {btnPos.x + ImGui::GetWindowPos().x,
                  btnPos.y + ImGui::GetWindowPos().y};

    ImVec4 bgCol = hov ? Col::DiscordHov : Col::Discord;
    dl->AddRectFilled(abs, {abs.x + sz, abs.y + sz}, U32(bgCol), 7.f);

    // Simple "D" icon inside the button
    float lineH = ImGui::GetTextLineHeight();
    dl->AddText({abs.x + (sz - ImGui::CalcTextSize("dc").x) * 0.5f,
                 abs.y + (sz - lineH) * 0.5f},
                U32(Col::TextMain), "dc");

    return clicked;
}

// ---------------------------------------------------------------------------
// Masthead (shared by login and main)
// ---------------------------------------------------------------------------
static void DrawMasthead(bool showStatusDot, bool connected, bool pending)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Accent gradient bar at top of window
    ImVec2 wpos = ImGui::GetWindowPos();
    float  ww   = ImGui::GetWindowWidth();
    dl->AddRectFilledMultiColor(
        wpos, {wpos.x + ww, wpos.y + 3.f},
        U32(Col::AccentDim), U32(Col::Accent),
        U32(Col::AccentDim), U32(Col::BgDeep));

    ImGui::Spacing();

    // Logo
    ImGui::PushStyleColor(ImGuiCol_Text, Col::Accent);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::Text("  \xe2\x9a\xa1 SPARKY");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();

    // Version + optional status dot to the right
    {
        float rightX = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;

        if (showStatusDot)
        {
            // Draw dot 18px to left of version text
            ImVec2 dotCenter = { wpos.x + rightX - 34.f,
                                  wpos.y + ImGui::GetStyle().WindowPadding.y + 12.f };
            DrawStatusDot(dl, dotCenter, connected, pending);
        }

        ImGui::SameLine(rightX - 24.f);
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
        ImGui::SetWindowFontScale(0.82f);
        ImGui::TextUnformatted("v3.0");
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    AccentSep();
    ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Login Screen
// ---------------------------------------------------------------------------
static void LoginScreen(UIState& state, std::function<void()>& onConnect)
{
    DrawMasthead(true, state.serverConnected, state.loggingIn);

    // ── Tab selector (Login / Sign Up) ────────────────────────────────────
    {
        const float btnW = (ImGui::GetContentRegionAvail().x - 8.f) * 0.5f;
        const float btnH = 30.f;

        ImVec4 loginBg  = !state.signUpMode ? ImVec4{0.04f,0.22f,0.34f,1.f}
                                             : ImVec4{0.06f,0.08f,0.16f,1.f};
        ImVec4 signupBg =  state.signUpMode ? ImVec4{0.04f,0.22f,0.34f,1.f}
                                            : ImVec4{0.06f,0.08f,0.16f,1.f};

        ImGui::PushStyleColor(ImGuiCol_Button,        loginBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Col::AccentDeep);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Col::AccentDim);
        if (ImGui::Button("Login", {btnW, btnH}))
        {
            state.signUpMode = false;
            state.loginError[0] = '\0';
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.f, 8.f);

        ImGui::PushStyleColor(ImGuiCol_Button,        signupBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Col::AccentDeep);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Col::AccentDim);
        if (ImGui::Button("Sign Up", {btnW, btnH}))
        {
            state.signUpMode = true;
            state.loginError[0] = '\0';
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // ── Form fields ───────────────────────────────────────────────────────
    const float fieldW = -1.f;

    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextLabel);
    ImGui::TextUnformatted("Username");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputText("##user", state.username, sizeof(state.username));

    ImGui::Spacing();

    if (state.signUpMode)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextLabel);
        ImGui::TextUnformatted("License Key");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("##lic", state.licenseKey, sizeof(state.licenseKey));
        ImGui::Spacing();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextLabel);
    ImGui::TextUnformatted("Password");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputText("##pass", state.password, sizeof(state.password),
                     ImGuiInputTextFlags_Password);

    if (state.signUpMode)
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextLabel);
        ImGui::TextUnformatted("Confirm Password");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("##passconf", state.passwordConf, sizeof(state.passwordConf),
                         ImGuiInputTextFlags_Password);
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // ── Error message ─────────────────────────────────────────────────────
    if (state.loginError[0] != '\0')
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::Red);
        ImGui::TextWrapped("%s", state.loginError);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // ── Submit button ─────────────────────────────────────────────────────
    {
        bool busy    = state.loggingIn;
        bool canTry  = !busy && state.username[0] != '\0' && state.password[0] != '\0'
                       && (!state.signUpMode || state.licenseKey[0] != '\0');

        ImVec4 btnBg = busy     ? ImVec4{0.05f,0.14f,0.20f,1.f}
                     : canTry   ? ImVec4{0.00f,0.42f,0.58f,1.f}
                                : ImVec4{0.07f,0.09f,0.18f,1.f};

        ImGui::BeginDisabled(!canTry);
        ImGui::PushStyleColor(ImGuiCol_Button,        btnBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.00f,0.62f,0.82f,1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Col::Accent);

        char lbl[48];
        snprintf(lbl, sizeof(lbl), "%s  %s",
                 busy ? Spinner() : "\xe2\x86\x92",
                 busy ? "Authenticating\xe2\x80\xa6"
                      : state.signUpMode ? "Create Account" : "Login");

        if (ImGui::Button(lbl, {-1.f, 36.f}))
        {
            // Validate sign-up confirm
            if (state.signUpMode && strncmp(state.password, state.passwordConf,
                                            sizeof(state.password)) != 0)
            {
                strncpy_s(state.loginError, sizeof(state.loginError),
                          "Passwords do not match.", _TRUNCATE);
            }
            else
            {
                state.loginError[0] = '\0';
                onConnect();
            }
        }
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    AccentSep();
    ImGui::Spacing();

    // ── Hint text ─────────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
    ImGui::SetWindowFontScale(0.82f);
    if (state.signUpMode)
        ImGui::TextWrapped("Enter your license key and choose a username/password to create your account.");
    else
        ImGui::TextWrapped("Enter your credentials to authenticate and access the loader.");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();

    // Discord icon bottom-right
    if (DiscordButton({ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
                       ImGui::GetWindowPos().y + ImGui::GetWindowHeight()}))
    {
        // Open Discord server — replace with your invite URL
        ShellExecuteW(nullptr, L"open", L"https://discord.gg/placeholder",
                      nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// ---------------------------------------------------------------------------
// Status mini-panel — used in main tabs
// ---------------------------------------------------------------------------
static void StatusPanel(const UIState& state)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::BgPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
    ImGui::BeginChild("##status", {0.f, 66.f}, true);
    ImGui::Spacing();

    char procText[80]{};
    if (state.processFound)
        snprintf(procText, sizeof(procText), "%s  (PID %u)", state.processName, state.targetPid);
    StatusRow("Process", state.processFound, false, procText, "", "Not found");

    ImGui::Spacing();

    char modText[48]{};
    if (state.dllReady && state.dllSizeBytes > 0)
    {
        char szBuf[16];
        FormatBytes(szBuf, sizeof(szBuf), state.dllSizeBytes);
        snprintf(modText, sizeof(modText), "Ready  (%s)", szBuf);
    }
    else if (state.dllReady)
        snprintf(modText, sizeof(modText), "Ready");

    StatusRow("Module", state.dllReady, state.connecting,
              modText, "Receiving\xe2\x80\xa6", "Awaiting download");

    ImGui::Spacing();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Preset card — full-width clickable card for Tab 1.
// ---------------------------------------------------------------------------
static bool PresetCard(int idx, const PresetInfo& p, bool selected, bool disabled)
{
    const float cardH = 62.f;
    const float cardW = ImGui::GetContentRegionAvail().x;

    ImGui::PushID(idx);
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::BeginDisabled(disabled);
    bool clicked = ImGui::InvisibleButton("##card", {cardW, cardH});
    ImGui::EndDisabled();
    bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t        = (float)ImGui::GetTime();

    // Animated glow when selected
    if (selected)
    {
        float glow = 0.10f + 0.08f * std::sinf(t * 2.5f);
        dl->AddRectFilled(pos, {pos.x+cardW, pos.y+cardH},
                          U32({Col::Accent.x, Col::Accent.y, Col::Accent.z, glow}), 9.f);
    }

    ImVec4 bg     = selected ? Col::BgCardSel : hovered ? Col::BgCardHov : Col::BgCard;
    ImVec4 border = selected ? Col::Accent    : hovered ? Col::AccentDim : Col::Border;
    float  bw     = selected ? 1.8f : 1.f;

    dl->AddRectFilled(pos, {pos.x+cardW, pos.y+cardH}, U32(bg), 8.f);
    dl->AddRect(pos, {pos.x+cardW, pos.y+cardH}, U32(border), 8.f, 0, bw);

    // Left accent stripe when selected
    if (selected)
        dl->AddRectFilled({pos.x, pos.y+10.f}, {pos.x+3.f, pos.y+cardH-10.f},
                          U32(Col::Accent), 2.f);

    float lineH = ImGui::GetTextLineHeight();
    float iconY = pos.y + (cardH * 0.5f) - lineH;
    ImVec4 iconCol = selected ? Lerp4(Col::AccentDim, Col::Accent,
                                       0.5f+0.5f*std::sinf(t*2.f))
                              : hovered ? Col::AccentDim : Col::Border;
    dl->AddText({pos.x + 14.f, iconY},  U32(iconCol), p.icon);
    dl->AddText({pos.x + 38.f, pos.y + 14.f}, U32(selected ? Col::Accent : Col::TextMain), p.name);
    dl->AddText({pos.x + 38.f, pos.y + 14.f + lineH + 4.f}, U32(Col::TextDim), p.desc);

    if (selected)
    {
        const char* check = "\xe2\x9c\x93";
        float cw = ImGui::CalcTextSize(check).x;
        dl->AddText({pos.x + cardW - cw - 14.f, pos.y + (cardH - lineH)*0.5f},
                    U32(Col::Green), check);
    }

    ImGui::PopID();
    return clicked;
}

// ---------------------------------------------------------------------------
// Tab 1 — Select (5 preset cards + auto-inject)
// ---------------------------------------------------------------------------
static void SelectTab(UIState& state, std::function<void()>& onConnect)
{
    ImGui::Spacing();
    StatusPanel(state);
    ImGui::Spacing();

    // Download progress
    if (state.connecting && state.downloadProgress > 0.f)
    {
        char pct[16];
        snprintf(pct, sizeof(pct), "%.0f%%", state.downloadProgress * 100.f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Col::AccentDim);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Col::BgDeep);
        ImGui::ProgressBar(state.downloadProgress, {-1.f, 5.f}, "");
        ImGui::PopStyleColor(2);
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
        ImGui::TextUnformatted(pct);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // Injected banner
    if (state.injected)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::Green);
        ImGui::TextUnformatted("  \xe2\x9c\x93  Injected successfully");
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    AccentSep();
    ImGui::Spacing();

    bool busy = state.connecting || state.injecting;
    for (int i = 0; i < 5; ++i)
    {
        if (PresetCard(i, kPresets[i], state.selectedPreset == i, busy))
        {
            state.selectedPreset    = i;
            state.autoInjectPending = true;
            state.injected          = false;
            state.dllReady          = false;
            state.downloadProgress  = 0.f;
            state.customTargetPid   = 0;
            onConnect();
        }
        ImGui::Spacing();
    }
}

// ---------------------------------------------------------------------------
// Process list refresh
// ---------------------------------------------------------------------------
static void RefreshProcessList(UIState& state)
{
    state.processList.clear();
    state.selectedProcessIdx = -1;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do {
            ProcessEntry entry{};
            entry.pid = pe.th32ProcessID;
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1,
                                entry.name, (int)sizeof(entry.name), nullptr, nullptr);
            state.processList.push_back(entry);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    std::sort(state.processList.begin(), state.processList.end(),
              [](const ProcessEntry& a, const ProcessEntry& b) {
                  return _stricmp(a.name, b.name) < 0;
              });
}

// ---------------------------------------------------------------------------
// Tab 2 — Inject (process list + DLL selector + inject button)
// ---------------------------------------------------------------------------
static void InjectTab(UIState& state, std::function<void()>& onConnect)
{
    static bool s_loaded = false;
    if (!s_loaded) { RefreshProcessList(state); s_loaded = true; }

    ImGui::Spacing();

    // ── Process list ──────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextLabel);
    ImGui::TextUnformatted("Running Processes");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 62.f);
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.07f,0.09f,0.18f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Col::AccentDeep);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Col::AccentDim);
    if (ImGui::SmallButton(" Refresh ")) RefreshProcessList(state);
    ImGui::PopStyleColor(3);
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::BgDeep);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.f);
    ImGui::BeginChild("##proclist", {0.f, 210.f}, true);

    if (state.processList.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
        ImGui::TextUnformatted("  No processes found — click Refresh");
        ImGui::PopStyleColor();
    }
    else
    {
        for (int i = 0; i < (int)state.processList.size(); ++i)
        {
            const auto& p = state.processList[i];
            bool sel = (state.selectedProcessIdx == i);

            char label[320];
            snprintf(label, sizeof(label), "  %-6u  %s", p.pid, p.name);

            ImGui::PushStyleColor(ImGuiCol_Header,        {0.05f,0.20f,0.30f,1.f});
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.06f,0.26f,0.38f,1.f});
            if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_SpanAllColumns))
                state.selectedProcessIdx = i;
            ImGui::PopStyleColor(2);

            if (sel && ImGui::IsWindowAppearing())
                ImGui::SetScrollHereY();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    AccentSep();
    ImGui::Spacing();

    // ── DLL selector ──────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextLabel);
    ImGui::TextUnformatted("DLL");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    for (int i = 0; i < 3; ++i)
    {
        if (i > 0) ImGui::SameLine(0.f, 14.f);
        ImGui::RadioButton(kPresets[i].name, &state.selectedCustomDll, i);
    }
    for (int i = 3; i < 5; ++i)
    {
        if (i > 3) ImGui::SameLine(0.f, 14.f);
        ImGui::RadioButton(kPresets[i].name, &state.selectedCustomDll, i);
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // ── Inject button ─────────────────────────────────────────────────────
    bool canInject = (state.selectedProcessIdx >= 0 &&
                      state.selectedProcessIdx < (int)state.processList.size());
    bool busy      = state.connecting || state.injecting;

    ImGui::BeginDisabled(!canInject || busy);
    ImVec4 btnBg = state.injected ? ImVec4{0.02f,0.28f,0.18f,1.f} :
                   busy           ? ImVec4{0.05f,0.14f,0.22f,1.f} :
                   canInject      ? ImVec4{0.00f,0.40f,0.55f,1.f} :
                                    ImVec4{0.07f,0.09f,0.18f,1.f};
    ImGui::PushStyleColor(ImGuiCol_Button,        btnBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.00f,0.62f,0.78f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Col::Accent);

    char injLabel[48];
    snprintf(injLabel, sizeof(injLabel), "%s  %s",
             busy           ? Spinner()              :
             state.injected ? "\xe2\x9c\x93"        : "\xe2\x96\xb6",
             busy           ? (state.connecting      ? "Fetching\xe2\x80\xa6"
                                                     : "Injecting\xe2\x80\xa6") :
             state.injected ? "Injected" : "INJECT");

    if (ImGui::Button(injLabel, {ImGui::GetContentRegionAvail().x, 36.f}) && canInject)
    {
        state.customTargetPid   = state.processList[state.selectedProcessIdx].pid;
        state.selectedPreset    = state.selectedCustomDll;
        state.autoInjectPending = true;
        state.injected          = false;
        state.dllReady          = false;
        state.downloadProgress  = 0.f;
        onConnect();
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
}

// ---------------------------------------------------------------------------
// Main panel (post-login): tab bar → Select / Inject
// ---------------------------------------------------------------------------
static void MainPanel(UIState& state,
                      std::function<void()>& onConnect,
                      std::function<void()>& onInject)
{
    DrawMasthead(true, state.serverConnected, state.connecting);

    // Username greeting
    if (state.username[0] != '\0')
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
        ImGui::SetWindowFontScale(0.85f);
        char greet[64];
        snprintf(greet, sizeof(greet), "  Welcome, %s", state.username);
        ImGui::TextUnformatted(greet);
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 7.f);
    if (ImGui::BeginTabBar("##tabs"))
    {
        if (ImGui::BeginTabItem("  Select  "))
        {
            SelectTab(state, onConnect);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("  Inject  "))
        {
            InjectTab(state, onConnect);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar();

    // Global auto-inject trigger (works for both tabs)
    bool processReady = state.processFound || (state.customTargetPid != 0);
    if (state.autoInjectPending && processReady &&
        state.dllReady && !state.injecting && !state.injected)
    {
        state.autoInjectPending = false;
        onInject();
    }

    // Discord icon
    if (DiscordButton({ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
                       ImGui::GetWindowPos().y + ImGui::GetWindowHeight()}))
    {
        ShellExecuteW(nullptr, L"open", L"https://discord.gg/placeholder",
                      nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// ---------------------------------------------------------------------------
// Full frame render
// ---------------------------------------------------------------------------
static void RenderFrame(UIState& state,
                        std::function<void()>& onConnect,
                        std::function<void()>& onInject)
{
    const ImGuiIO& io = ImGui::GetIO();

    // Background fill
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##bg", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove   |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
    ImGui::End();
    ImGui::PopStyleVar(2);

    // Main panel dimensions
    constexpr float W = 500.f;
    constexpr float H = 610.f;
    ImGui::SetNextWindowPos(
        {std::floor((io.DisplaySize.x - W) * 0.5f),
         std::floor((io.DisplaySize.y - H) * 0.5f)},
        ImGuiCond_Always);
    ImGui::SetNextWindowSize({W, H}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({W, H}, {W, H});

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleColor(ImGuiCol_Border, Col::AccentDim);

    ImGui::Begin("##main", nullptr,
        ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove   |
        ImGuiWindowFlags_NoTitleBar);

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (!state.loggedIn)
        LoginScreen(state, onConnect);
    else
        MainPanel(state, onConnect, onInject);

    ImGui::End();
}

// ---------------------------------------------------------------------------
// RunUI
// ---------------------------------------------------------------------------
void RunUI(UIState& state,
           std::function<void()> onConnect,
           std::function<void()> onInject)
{
    glfwSetErrorCallback([](int, const char*) {});
    if (!glfwInit()) return;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    GLFWmonitor*       mon = glfwGetPrimaryMonitor();
    const GLFWvidmode*  vm = mon ? glfwGetVideoMode(mon) : nullptr;

    GLFWwindow* win = glfwCreateWindow(500, 610, "Sparky", nullptr, nullptr);
    if (!win) { glfwTerminate(); return; }

    if (vm && mon)
    {
        int mx, my;
        glfwGetMonitorPos(mon, &mx, &my);
        glfwSetWindowPos(win, mx + (vm->width  - 500) / 2,
                              my + (vm->height - 610) / 2);
    }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyTheme();

    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    while (!glfwWindowShouldClose(win))
    {
        // Sleep until an event arrives or 33 ms elapse (≈30 fps cap).
        // This lets the GPU idle when the UI is static instead of spinning at
        // vsync rate. Animations still update via the periodic timeout.
        glfwWaitEventsTimeout(1.0 / 30.0);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        RenderFrame(state, onConnect, onInject);

        ImGui::Render();
        int fw, fh;
        glfwGetFramebufferSize(win, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glClearColor(Col::Bg.x, Col::Bg.y, Col::Bg.z, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
}
