#include "UI.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include <mutex>
#include <algorithm>
#include <format>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstring>

static std::mutex g_logMutex;

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
// Colour palette
// ---------------------------------------------------------------------------
namespace Col
{
    static constexpr ImVec4 Bg          = {0.05f,  0.05f,  0.09f,  1.f};
    static constexpr ImVec4 BgChild     = {0.04f,  0.04f,  0.08f,  1.f};
    static constexpr ImVec4 BgDeep      = {0.03f,  0.03f,  0.06f,  1.f};
    static constexpr ImVec4 BgPanel     = {0.06f,  0.07f,  0.13f,  1.f};

    static constexpr ImVec4 Border      = {0.14f,  0.17f,  0.30f,  1.f};
    static constexpr ImVec4 BorderDim   = {0.10f,  0.12f,  0.22f,  0.7f};

    static constexpr ImVec4 Accent      = {0.00f,  0.75f,  0.95f,  1.f};
    static constexpr ImVec4 AccentHover = {0.15f,  0.85f,  1.00f,  1.f};
    static constexpr ImVec4 AccentDim   = {0.00f,  0.50f,  0.65f,  1.f};
    static constexpr ImVec4 AccentDeep  = {0.00f,  0.35f,  0.48f,  1.f};

    static constexpr ImVec4 Green       = {0.10f,  0.85f,  0.45f,  1.f};
    static constexpr ImVec4 GreenDim    = {0.05f,  0.60f,  0.30f,  1.f};
    static constexpr ImVec4 Red         = {0.90f,  0.25f,  0.25f,  1.f};
    static constexpr ImVec4 Orange      = {1.00f,  0.65f,  0.10f,  1.f};
    static constexpr ImVec4 Yellow      = {1.00f,  0.85f,  0.15f,  1.f};

    static constexpr ImVec4 TextMain    = {0.88f,  0.88f,  0.93f,  1.f};
    static constexpr ImVec4 TextDim     = {0.45f,  0.47f,  0.60f,  1.f};
    static constexpr ImVec4 TextLabel   = {0.55f,  0.58f,  0.75f,  1.f};
    static constexpr ImVec4 TextDisable = {0.30f,  0.30f,  0.40f,  1.f};
}

static ImU32 U32(const ImVec4& v)
{
    return ImGui::ColorConvertFloat4ToU32(v);
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
static void ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowPadding      = {16.f, 16.f};
    s.FramePadding       = {10.f,  6.f};
    s.CellPadding        = { 8.f,  4.f};
    s.ItemSpacing        = { 8.f,  7.f};
    s.ItemInnerSpacing   = { 6.f,  4.f};
    s.IndentSpacing      = 18.f;
    s.ScrollbarSize      = 10.f;
    s.GrabMinSize        = 10.f;

    s.WindowBorderSize   = 1.f;
    s.FrameBorderSize    = 1.f;
    s.PopupBorderSize    = 1.f;
    s.TabBorderSize      = 0.f;

    s.WindowRounding     = 10.f;
    s.ChildRounding      = 7.f;
    s.FrameRounding      = 6.f;
    s.ScrollbarRounding  = 5.f;
    s.GrabRounding       = 5.f;
    s.TabRounding        = 6.f;
    s.PopupRounding      = 8.f;

    ImVec4* c = s.Colors;

    c[ImGuiCol_WindowBg]             = Col::Bg;
    c[ImGuiCol_PopupBg]              = Col::BgPanel;
    c[ImGuiCol_ChildBg]              = Col::BgChild;

    c[ImGuiCol_Border]               = Col::Border;
    c[ImGuiCol_BorderShadow]         = {0.f, 0.f, 0.f, 0.f};

    c[ImGuiCol_TitleBg]              = Col::BgDeep;
    c[ImGuiCol_TitleBgActive]        = Col::BgDeep;
    c[ImGuiCol_TitleBgCollapsed]     = Col::BgDeep;

    c[ImGuiCol_Text]                 = Col::TextMain;
    c[ImGuiCol_TextDisabled]         = Col::TextDisable;

    c[ImGuiCol_FrameBg]              = {0.09f, 0.10f, 0.18f, 1.f};
    c[ImGuiCol_FrameBgHovered]       = {0.13f, 0.15f, 0.26f, 1.f};
    c[ImGuiCol_FrameBgActive]        = {0.16f, 0.19f, 0.32f, 1.f};

    c[ImGuiCol_CheckMark]            = Col::Accent;
    c[ImGuiCol_SliderGrab]           = Col::AccentDim;
    c[ImGuiCol_SliderGrabActive]     = Col::Accent;

    c[ImGuiCol_Button]               = {0.08f, 0.10f, 0.20f, 1.f};
    c[ImGuiCol_ButtonHovered]        = Col::AccentDeep;
    c[ImGuiCol_ButtonActive]         = Col::AccentDim;

    c[ImGuiCol_Header]               = {0.09f, 0.11f, 0.22f, 1.f};
    c[ImGuiCol_HeaderHovered]        = {0.12f, 0.15f, 0.28f, 1.f};
    c[ImGuiCol_HeaderActive]         = Col::AccentDim;

    c[ImGuiCol_Separator]            = Col::BorderDim;
    c[ImGuiCol_SeparatorHovered]     = Col::AccentDim;
    c[ImGuiCol_SeparatorActive]      = Col::Accent;

    c[ImGuiCol_ResizeGrip]           = {0.f, 0.f, 0.f, 0.f};
    c[ImGuiCol_ResizeGripHovered]    = Col::AccentDim;
    c[ImGuiCol_ResizeGripActive]     = Col::Accent;

    c[ImGuiCol_ScrollbarBg]          = Col::BgDeep;
    c[ImGuiCol_ScrollbarGrab]        = Col::Border;
    c[ImGuiCol_ScrollbarGrabHovered] = Col::AccentDim;
    c[ImGuiCol_ScrollbarGrabActive]  = Col::Accent;

    c[ImGuiCol_PlotLines]            = Col::Accent;
    c[ImGuiCol_PlotHistogram]        = Col::AccentDim;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void StatusRow(const char* label, bool active, bool pending,
                      const char* activeText, const char* pendingText, const char* offText)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p       = ImGui::GetCursorScreenPos();

    float cx = p.x + 7.f;
    float cy = p.y + 9.f;

    ImVec4 dotCol = active  ? Col::Green :
                    pending ? Col::Orange :
                              Col::Red;

    if (active || pending)
        dl->AddCircleFilled({cx, cy}, 7.f, U32(ImVec4{dotCol.x, dotCol.y, dotCol.z, 0.18f}));
    dl->AddCircleFilled({cx, cy}, 4.5f, U32(dotCol));

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.f);

    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextLabel);
    ImGui::Text("%-10s", label);
    ImGui::PopStyleColor();

    ImGui::SameLine(0.f, 6.f);
    ImGui::PushStyleColor(ImGuiCol_Text, active  ? Col::TextMain  :
                                          pending ? Col::Orange    : Col::TextDim);
    ImGui::TextUnformatted(active ? activeText : pending ? pendingText : offText);
    ImGui::PopStyleColor();
}

static void AccentSep()
{
    ImGui::PushStyleColor(ImGuiCol_Separator, Col::AccentDim);
    ImGui::Separator();
    ImGui::PopStyleColor();
}

static const char* Spinner()
{
    static const char* frames[] = {"◐","◓","◑","◒"};
    return frames[(int)(ImGui::GetTime() * 6.0) & 3];
}

static void FormatBytes(char* buf, size_t n, uint32_t bytes)
{
    if (bytes >= 1024*1024)
        snprintf(buf, n, "%.1f MB", bytes / (1024.f*1024.f));
    else if (bytes >= 1024)
        snprintf(buf, n, "%.1f KB", bytes / 1024.f);
    else
        snprintf(buf, n, "%u B", bytes);
}

// ---------------------------------------------------------------------------
// Main inject panel — rendered directly, no tab bar
// ---------------------------------------------------------------------------
static void InjectPanel(UIState& state,
                        std::function<void()>& onConnect,
                        std::function<void()>& onInject)
{
    ImGui::Spacing();

    // ── Status panel ─────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::BgPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.f);
    ImGui::BeginChild("##status", {0.f, 64.f}, true);
    ImGui::Spacing();

    char procText[64]{};
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

    StatusRow("Module",  state.dllReady, state.connecting, modText,
              "Receiving…", "Not ready");

    ImGui::Spacing();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // ── Target process ────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextLabel);
    ImGui::TextUnformatted("Target Process");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText("##proc", state.processName, sizeof(state.processName));

    ImGui::Spacing();

    // ── Download progress bar ─────────────────────────────────────────────
    if (state.connecting && state.downloadProgress > 0.f)
    {
        char pctBuf[32];
        snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", state.downloadProgress * 100.f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Col::AccentDim);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,       Col::BgDeep);
        ImGui::ProgressBar(state.downloadProgress, {-1.f, 6.f}, "");
        ImGui::PopStyleColor(2);
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
        ImGui::TextUnformatted(pctBuf);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    AccentSep();
    ImGui::Spacing();

    // ── Action buttons ────────────────────────────────────────────────────
    const float avail = ImGui::GetContentRegionAvail().x;
    const float btnW  = (avail - 12.f) * 0.5f;
    const float btnH  = 36.f;

    // Connect button
    {
        bool busy    = state.connecting;
        bool already = state.serverConnected && !busy;

        ImVec4 bg = already ? ImVec4{0.03f, 0.30f, 0.38f, 1.f} :
                    busy    ? ImVec4{0.05f, 0.15f, 0.22f, 1.f} :
                              ImVec4{0.07f, 0.10f, 0.20f, 1.f};

        ImGui::BeginDisabled(busy);
        ImGui::PushStyleColor(ImGuiCol_Button,        bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Col::AccentDeep);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Col::AccentDim);

        char connLabel[32];
        snprintf(connLabel, sizeof(connLabel), "%s  Connect",
                 busy ? Spinner() : already ? "✓" : "↗");

        if (ImGui::Button(connLabel, {btnW, btnH}))
            onConnect();

        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
    }

    ImGui::SameLine(0.f, 12.f);

    // Inject button
    {
        bool canInject = state.processFound && state.dllReady
                      && !state.injected && !state.injecting;

        ImVec4 bg = state.injected   ? ImVec4{0.02f, 0.28f, 0.18f, 1.f} :
                    state.injecting  ? ImVec4{0.05f, 0.15f, 0.22f, 1.f} :
                    canInject        ? ImVec4{0.00f, 0.40f, 0.52f, 1.f} :
                                       ImVec4{0.07f, 0.10f, 0.20f, 1.f};

        ImGui::BeginDisabled(!canInject && !state.injecting);
        ImGui::PushStyleColor(ImGuiCol_Button,        bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.00f, 0.60f, 0.75f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Col::Accent);

        char injLabel[32];
        snprintf(injLabel, sizeof(injLabel), "%s  %s",
                 state.injecting ? Spinner() : state.injected ? "✓" : "▶",
                 state.injecting ? "Injecting" : state.injected ? "Injected" : "Inject");

        if (ImGui::Button(injLabel, {btnW, btnH}))
            onInject();

        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
    }
}

// ---------------------------------------------------------------------------
// Log panel
// ---------------------------------------------------------------------------
static void LogPanel(UIState& state)
{
    ImGui::PushStyleColor(ImGuiCol_Text, Col::TextLabel);
    ImGui::TextUnformatted("Log");
    ImGui::PopStyleColor();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 44.f);
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.08f, 0.10f, 0.18f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Col::AccentDeep);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Col::AccentDim);
    if (ImGui::SmallButton("  Clear  "))
    {
        std::lock_guard lk(g_logMutex);
        state.logLines.clear();
    }
    ImGui::PopStyleColor(3);

    const float logH = ImGui::GetContentRegionAvail().y - 6.f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::BgDeep);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
    ImGui::BeginChild("##log", {0.f, logH}, true);

    {
        std::lock_guard lk(g_logMutex);
        for (const auto& line : state.logLines)
        {
            ImVec4 col = Col::TextMain;
            if (line.find("[ERR]") != std::string::npos ||
                line.find("fail")  != std::string::npos ||
                line.find("FAIL")  != std::string::npos)
                col = Col::Red;
            else if (line.find("[INF]") != std::string::npos ||
                     line.find("success") != std::string::npos ||
                     line.find("OK")      != std::string::npos)
                col = Col::Green;
            else if (line.find("[WRN]") != std::string::npos)
                col = Col::Yellow;
            else if (line.find("DETECT:") != std::string::npos)
                col = Col::Orange;

            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f)
        ImGui::SetScrollHereY(1.f);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Full frame render
// ---------------------------------------------------------------------------
static void RenderFrame(UIState& state,
                        std::function<void()>& onConnect,
                        std::function<void()>& onInject)
{
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##bg", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove   |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
    ImGui::End();
    ImGui::PopStyleVar(2);

    // ── Main panel ─────────────────────────────────────────────────────────
    constexpr float W = 480.f;
    constexpr float H = 580.f;
    ImGui::SetNextWindowPos(
        {std::floor((io.DisplaySize.x - W) * 0.5f),
         std::floor((io.DisplaySize.y - H) * 0.5f)},
        ImGuiCond_Always);
    ImGui::SetNextWindowSize({W, H}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({W, H}, {W, H});

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleColor(ImGuiCol_Border, Col::AccentDim);

    ImGui::Begin("##main", nullptr,
        ImGuiWindowFlags_NoCollapse  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoTitleBar);

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // ── Masthead ────────────────────────────────────────────────────────────
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::Accent);
        ImGui::SetWindowFontScale(1.55f);
        ImGui::Text("  \u26A1 SPARKY");
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();

        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x - 44.f);
        ImGui::PushStyleColor(ImGuiCol_Text, Col::TextDim);
        ImGui::SetWindowFontScale(0.85f);
        ImGui::TextUnformatted("v3.0");
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        AccentSep();
        ImGui::Spacing();
    }

    // ── Inject panel (no tab bar) ────────────────────────────────────────────
    InjectPanel(state, onConnect, onInject);

    ImGui::Spacing();
    AccentSep();
    ImGui::Spacing();

    // ── Log panel ───────────────────────────────────────────────────────────
    LogPanel(state);

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
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED,  GLFW_TRUE);

    GLFWmonitor* mon      = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;

    GLFWwindow* win = glfwCreateWindow(480, 580, "Sparky", nullptr, nullptr);
    if (!win) { glfwTerminate(); return; }

    if (vm && mon)
    {
        int mx, my;
        glfwGetMonitorPos(mon, &mx, &my);
        glfwSetWindowPos(win, mx + (vm->width  - 480) / 2,
                              my + (vm->height - 580) / 2);
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
        glfwPollEvents();

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
