#include "UI.h"

// ImGui + GLFW + OpenGL3 backend
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include <mutex>
#include <algorithm>
#include <format>
#include <chrono>
#include <ctime>

static std::mutex g_logMutex;

void UIState::AddLog(const std::string& msg)
{
    // Timestamp prefix
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char ts[16]{};
    struct tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    snprintf(ts, sizeof(ts), "[%02d:%02d:%02d] ",
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    std::lock_guard lk(g_logMutex);
    logLines.push_back(std::string(ts) + msg);
    if (logLines.size() > 200)
        logLines.erase(logLines.begin());
}

// ---------------------------------------------------------------------------
// Style — dark navy / cyan accent, rounded, minimalist
// ---------------------------------------------------------------------------
static void ApplyStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowPadding      = {14.f, 14.f};
    s.FramePadding       = {8.f,  5.f};
    s.ItemSpacing        = {8.f,  6.f};
    s.ItemInnerSpacing   = {6.f,  4.f};
    s.ScrollbarSize      = 10.f;
    s.GrabMinSize        = 10.f;
    s.WindowBorderSize   = 1.f;
    s.FrameBorderSize    = 0.f;
    s.WindowRounding     = 8.f;
    s.FrameRounding      = 5.f;
    s.ScrollbarRounding  = 4.f;
    s.GrabRounding       = 4.f;
    s.TabRounding        = 5.f;
    s.PopupRounding      = 6.f;

    ImVec4* c = s.Colors;
    // Background
    c[ImGuiCol_WindowBg]         = {0.05f, 0.05f, 0.09f, 1.f};
    c[ImGuiCol_PopupBg]          = {0.06f, 0.06f, 0.11f, 1.f};
    c[ImGuiCol_ChildBg]          = {0.04f, 0.04f, 0.08f, 1.f};

    // Borders
    c[ImGuiCol_Border]           = {0.15f, 0.17f, 0.28f, 1.f};
    c[ImGuiCol_BorderShadow]     = {0.f,   0.f,   0.f,   0.f};

    // Title
    c[ImGuiCol_TitleBg]          = {0.04f, 0.04f, 0.08f, 1.f};
    c[ImGuiCol_TitleBgActive]    = {0.04f, 0.04f, 0.08f, 1.f};
    c[ImGuiCol_TitleBgCollapsed] = {0.04f, 0.04f, 0.08f, 1.f};

    // Text
    c[ImGuiCol_Text]             = {0.88f, 0.88f, 0.92f, 1.f};
    c[ImGuiCol_TextDisabled]     = {0.4f,  0.4f,  0.5f,  1.f};

    // Frame (input boxes, etc.)
    c[ImGuiCol_FrameBg]          = {0.10f, 0.11f, 0.18f, 1.f};
    c[ImGuiCol_FrameBgHovered]   = {0.14f, 0.15f, 0.25f, 1.f};
    c[ImGuiCol_FrameBgActive]    = {0.16f, 0.18f, 0.30f, 1.f};

    // Accent (cyan electric blue)
    constexpr ImVec4 accent      = {0.0f,  0.78f, 0.94f, 1.f};
    constexpr ImVec4 accentHover = {0.15f, 0.85f, 1.0f,  1.f};
    constexpr ImVec4 accentDim   = {0.0f,  0.55f, 0.68f, 1.f};

    c[ImGuiCol_CheckMark]        = accent;
    c[ImGuiCol_SliderGrab]       = accent;
    c[ImGuiCol_SliderGrabActive] = accentHover;

    // Buttons
    c[ImGuiCol_Button]           = {0.08f, 0.10f, 0.18f, 1.f};
    c[ImGuiCol_ButtonHovered]    = {0.0f,  0.55f, 0.68f, 1.f};
    c[ImGuiCol_ButtonActive]     = accent;

    // Headers
    c[ImGuiCol_Header]           = {0.10f, 0.12f, 0.22f, 1.f};
    c[ImGuiCol_HeaderHovered]    = {0.12f, 0.15f, 0.28f, 1.f};
    c[ImGuiCol_HeaderActive]     = accentDim;

    // Separator
    c[ImGuiCol_Separator]        = {0.15f, 0.17f, 0.28f, 1.f};

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]      = {0.04f, 0.04f, 0.08f, 1.f};
    c[ImGuiCol_ScrollbarGrab]    = {0.15f, 0.17f, 0.28f, 1.f};
    c[ImGuiCol_ScrollbarGrabHovered] = accentDim;
    c[ImGuiCol_ScrollbarGrabActive]  = accent;

    // Tab
    c[ImGuiCol_Tab]              = {0.08f, 0.09f, 0.15f, 1.f};
    c[ImGuiCol_TabHovered]       = accentDim;
    c[ImGuiCol_TabActive]        = {0.0f,  0.60f, 0.75f, 1.f};
    c[ImGuiCol_TabUnfocused]     = c[ImGuiCol_Tab];
    c[ImGuiCol_TabUnfocusedActive] = c[ImGuiCol_TabActive];
}

// ---------------------------------------------------------------------------
// Status dot helper
// ---------------------------------------------------------------------------
static void StatusDot(bool active, const char* label)
{
    constexpr ImVec4 green  = {0.0f,  0.85f, 0.45f, 1.f};
    constexpr ImVec4 red    = {0.95f, 0.25f, 0.25f, 1.f};

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    p.x += 4.f; p.y += 7.f;
    dl->AddCircleFilled(p, 5.f, ImGui::ColorConvertFloat4ToU32(active ? green : red));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16.f);
    ImGui::TextUnformatted(label);
}

// ---------------------------------------------------------------------------
// Main UI render — called each frame
// ---------------------------------------------------------------------------
static void RenderFrame(UIState& state,
                        std::function<void()>& onConnect,
                        std::function<void()>& onInject)
{
    const ImGuiIO& io = ImGui::GetIO();

    // Full-screen dockspace background window
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

    // Main panel (centered)
    constexpr float W = 480.f, H = 560.f;
    ImGui::SetNextWindowPos({(io.DisplaySize.x - W) * 0.5f,
                             (io.DisplaySize.y - H) * 0.5f},
                            ImGuiCond_Once);
    ImGui::SetNextWindowSize({W, H}, ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints({W, H}, {W, H});

    ImGui::Begin("##main",
                 nullptr,
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoResize   |
                 ImGuiWindowFlags_NoScrollbar);

    // ---- Header -----------------------------------------------------------
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.0f, 0.78f, 0.94f, 1.f});
        ImGui::SetWindowFontScale(1.45f);
        ImGui::Text("  SPARKY");
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40.f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.4f, 0.4f, 0.5f, 1.f});
        ImGui::Text("v1.0");
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4{0.0f, 0.55f, 0.68f, 0.6f});
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // ---- Settings tab / Status tab ----------------------------------------
    if (ImGui::BeginTabBar("##tabs"))
    {
        // === INJECT tab ===
        if (ImGui::BeginTabItem("Inject"))
        {
            ImGui::Spacing();

            // Status block
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.06f, 0.07f, 0.12f, 1.f});
            ImGui::BeginChild("##status", {0.f, 80.f}, true);
            ImGui::Spacing();

            StatusDot(state.processFound,
                      state.processFound
                          ? std::format("Process  |  PID {}", state.targetPid).c_str()
                          : "Process  |  Waiting...");

            ImGui::Spacing();
            StatusDot(state.serverConnected,
                      state.serverConnected ? "Server   |  Connected" : "Server   |  Offline");

            ImGui::Spacing();
            StatusDot(state.dllReady,
                      state.dllReady ? "Module   |  Ready" : "Module   |  Not found");

            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Settings inline
            ImGui::PushItemWidth(-1.f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.5f, 0.55f, 0.7f, 1.f});
            ImGui::Text("Target Process");
            ImGui::PopStyleColor();
            ImGui::InputText("##proc", state.processName, sizeof(state.processName));

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.5f, 0.55f, 0.7f, 1.f});
            ImGui::Text("Module Path");
            ImGui::PopStyleColor();
            ImGui::InputText("##dll", state.dllPath, sizeof(state.dllPath));
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Action buttons
            float btnW = (ImGui::GetContentRegionAvail().x - 12.f) * 0.5f;

            ImGui::PushStyleColor(ImGuiCol_Button,
                state.serverConnected
                    ? ImVec4{0.04f, 0.35f, 0.42f, 1.f}
                    : ImVec4{0.08f, 0.10f, 0.18f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.0f, 0.55f, 0.68f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.0f, 0.78f, 0.94f, 1.f});

            if (ImGui::Button("Connect", {btnW, 38.f}))
                onConnect();

            ImGui::PopStyleColor(3);
            ImGui::SameLine(0.f, 12.f);

            bool canInject = state.processFound && state.dllReady && !state.injected;
            ImGui::BeginDisabled(!canInject);

            ImGui::PushStyleColor(ImGuiCol_Button,
                canInject
                    ? ImVec4{0.0f, 0.45f, 0.55f, 1.f}
                    : ImVec4{0.08f, 0.10f, 0.18f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.0f, 0.65f, 0.8f,  1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.0f, 0.85f, 1.0f,  1.f});

            if (ImGui::Button(state.injected ? "Injected " : "Inject  ", {btnW, 38.f}))
                onInject();

            ImGui::PopStyleColor(3);
            ImGui::EndDisabled();

            ImGui::EndTabItem();
        }

        // === SERVER tab ===
        if (ImGui::BeginTabItem("Server"))
        {
            ImGui::Spacing();

            ImGui::PushItemWidth(-1.f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.5f, 0.55f, 0.7f, 1.f});
            ImGui::Text("Host");
            ImGui::PopStyleColor();
            ImGui::InputText("##host", state.serverHost, sizeof(state.serverHost));

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.5f, 0.55f, 0.7f, 1.f});
            ImGui::Text("Port");
            ImGui::PopStyleColor();
            ImGui::InputInt("##port", &state.serverPort, 0);
            state.serverPort = std::max(1, std::min(65535, state.serverPort));
            ImGui::PopItemWidth();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- Log panel --------------------------------------------------------
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.5f, 0.55f, 0.7f, 1.f});
        ImGui::Text("Log");
        ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 38.f);
        if (ImGui::SmallButton("Clear"))
        {
            std::lock_guard lk(g_logMutex);
            state.logLines.clear();
        }
        ImGui::Spacing();

        float logH = ImGui::GetContentRegionAvail().y - 8.f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.03f, 0.04f, 0.07f, 1.f});
        ImGui::BeginChild("##log", {0.f, logH}, true);

        {
            std::lock_guard lk(g_logMutex);
            for (const auto& line : state.logLines)
            {
                // Color-code by prefix
                ImVec4 col = {0.72f, 0.72f, 0.76f, 1.f};
                if (line.find("[ERR]") != std::string::npos ||
                    line.find("fail") != std::string::npos)
                    col = {0.95f, 0.35f, 0.35f, 1.f};
                else if (line.find("[INF]") != std::string::npos ||
                         line.find("success") != std::string::npos)
                    col = {0.4f, 0.9f, 0.55f, 1.f};
                else if (line.find("[WRN]") != std::string::npos)
                    col = {1.f, 0.75f, 0.2f, 1.f};

                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
        }

        // Auto-scroll
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.f)
            ImGui::SetScrollHereY(1.f);

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// RunUI — GLFW + ImGui event loop
// ---------------------------------------------------------------------------
void RunUI(UIState& state,
           std::function<void()> onConnect,
           std::function<void()> onInject)
{
    glfwSetErrorCallback([](int, const char* desc) {
        // silent — we log via UIState
        (void)desc;
    });

    if (!glfwInit()) return;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(480, 560, "Sparky", nullptr, nullptr);
    if (!window) { glfwTerminate(); return; }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // no imgui.ini
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyStyle();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        RenderFrame(state, onConnect, onInject);

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.05f, 0.05f, 0.09f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}
