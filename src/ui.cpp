// ui.cpp — the overlay: menu bar, control panel, Learn panel, HUD, orbit viewer.
#include "app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <random>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

namespace fs = std::filesystem;

// ------------------------------------------------------------------ setup & themes
void App::setupImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    static std::string ini = (userDir / "imgui.ini").string();
    io.IniFilename = cli.hidden ? nullptr : ini.c_str();  // headless runs must not move the user's panels
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 460");

    std::error_code ec;
    auto firstExisting = [&](std::initializer_list<const char*> paths) -> const char* {
        for (auto* p : paths)
            if (fs::exists(p, ec)) return p;
        return nullptr;
    };
    const char* ui = firstExisting({"/usr/share/fonts/noto/NotoSans-Medium.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                                    "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf"});
    const char* mono = firstExisting({"/usr/share/fonts/noto/NotoSansMono-Regular.ttf", "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
                                      "/usr/share/fonts/TTF/Hack-Regular.ttf"});
    fontRetro = io.Fonts->AddFontDefault();  // ProggyClean: a crisp bitmap-style font, fitting for DOS nostalgia
    fontUI = ui ? io.Fonts->AddFontFromFileTTF(ui, 17.0f) : fontRetro;
    fontMono = mono ? io.Fonts->AddFontFromFileTTF(mono, 16.0f) : fontRetro;
    applyTheme();
}

void App::applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGuiIO& io = ImGui::GetIO();
    float dpi = ImGui_ImplGlfw_GetContentScaleForWindow(win);
    ImVec4* c = s.Colors;
    if (uiTheme == 1) {
        // Fractint on a VGA card: EGA blue, white text, cyan and yellow highlights, square corners.
        ImGui::StyleColorsClassic();
        auto rgb = [](float r, float g, float b, float a = 1.0f) { return ImVec4(r, g, b, a); };
        const ImVec4 blue = rgb(0, 0, 0.667f, 0.95f), dblue = rgb(0, 0, 0.42f), cyan = rgb(0, 0.667f, 0.667f),
                     lcyan = rgb(0.333f, 1, 1), yellow = rgb(1, 1, 0.333f), grey = rgb(0.667f, 0.667f, 0.667f);
        c[ImGuiCol_Text] = rgb(1, 1, 1);
        c[ImGuiCol_TextDisabled] = grey;
        c[ImGuiCol_WindowBg] = blue;
        c[ImGuiCol_ChildBg] = rgb(0, 0, 0, 0);
        c[ImGuiCol_PopupBg] = rgb(0, 0, 0.55f, 0.98f);
        c[ImGuiCol_Border] = grey;
        c[ImGuiCol_FrameBg] = dblue;
        c[ImGuiCol_FrameBgHovered] = rgb(0, 0.33f, 0.667f);
        c[ImGuiCol_FrameBgActive] = rgb(0, 0.5f, 0.667f);
        c[ImGuiCol_TitleBg] = dblue;
        c[ImGuiCol_TitleBgActive] = rgb(0, 0.42f, 0.55f);
        c[ImGuiCol_TitleBgCollapsed] = dblue;
        c[ImGuiCol_MenuBarBg] = rgb(0, 0, 0.5f);
        c[ImGuiCol_ScrollbarBg] = dblue;
        c[ImGuiCol_ScrollbarGrab] = cyan;
        c[ImGuiCol_CheckMark] = yellow;
        c[ImGuiCol_SliderGrab] = cyan;
        c[ImGuiCol_SliderGrabActive] = yellow;
        c[ImGuiCol_Button] = rgb(0, 0.5f, 0.6f);
        c[ImGuiCol_ButtonHovered] = cyan;
        c[ImGuiCol_ButtonActive] = rgb(0.6f, 0.6f, 0.2f);
        c[ImGuiCol_Header] = rgb(0, 0.5f, 0.6f, 0.7f);
        c[ImGuiCol_HeaderHovered] = cyan;
        c[ImGuiCol_HeaderActive] = rgb(0.6f, 0.6f, 0.2f);
        c[ImGuiCol_Separator] = grey;
        c[ImGuiCol_Tab] = dblue;
        c[ImGuiCol_TabHovered] = cyan;
        c[ImGuiCol_TabSelected] = rgb(0, 0.5f, 0.6f);
        c[ImGuiCol_TabSelectedOverline] = yellow;
        c[ImGuiCol_TabDimmed] = dblue;
        c[ImGuiCol_TabDimmedSelected] = rgb(0, 0.4f, 0.5f);
        c[ImGuiCol_PlotHistogram] = yellow;
        c[ImGuiCol_TextSelectedBg] = rgb(0, 0.667f, 0.667f, 0.5f);
        c[ImGuiCol_TextLink] = lcyan;
        s.WindowRounding = s.FrameRounding = s.GrabRounding = s.TabRounding = s.PopupRounding = s.ScrollbarRounding = 0;
        s.WindowBorderSize = 2.0f;
        s.FrameBorderSize = 0.0f;
        s.FontSizeBase = 13.0f * std::max(1.0f, std::round(uiScale * dpi));
        s.FontScaleMain = 1.0f;
        s.FontScaleDpi = 1.0f;  // integer-scaled bitmap font stays crisp
        io.FontDefault = fontRetro;
        s.ScaleAllSizes(std::max(1.0f, std::round(uiScale * dpi)));
    } else {
        ImGui::StyleColorsDark();
        const ImVec4 accent(0.42f, 0.58f, 1.0f, 1.0f);
        auto a = [&](float alpha) { return ImVec4(accent.x, accent.y, accent.z, alpha); };
        c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.06f, 0.075f, 0.86f);
        c[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.075f, 0.09f, 0.97f);
        c[ImGuiCol_Border] = ImVec4(1, 1, 1, 0.08f);
        c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 0.06f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, 0.10f);
        c[ImGuiCol_FrameBgActive] = a(0.30f);
        c[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.9f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.09f, 0.12f, 0.95f);
        c[ImGuiCol_MenuBarBg] = ImVec4(0.04f, 0.045f, 0.055f, 0.88f);
        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = a(0.85f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.75f, 0.82f, 1.0f, 1.0f);
        c[ImGuiCol_Button] = a(0.22f);
        c[ImGuiCol_ButtonHovered] = a(0.45f);
        c[ImGuiCol_ButtonActive] = a(0.65f);
        c[ImGuiCol_Header] = a(0.20f);
        c[ImGuiCol_HeaderHovered] = a(0.35f);
        c[ImGuiCol_HeaderActive] = a(0.50f);
        c[ImGuiCol_Tab] = ImVec4(1, 1, 1, 0.04f);
        c[ImGuiCol_TabHovered] = a(0.40f);
        c[ImGuiCol_TabSelected] = a(0.28f);
        c[ImGuiCol_TabSelectedOverline] = accent;
        c[ImGuiCol_TabDimmed] = ImVec4(1, 1, 1, 0.03f);
        c[ImGuiCol_TabDimmedSelected] = a(0.18f);
        c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.10f);
        c[ImGuiCol_PlotHistogram] = accent;
        c[ImGuiCol_TextLink] = accent;
        s.WindowRounding = 8.0f;
        s.ChildRounding = 6.0f;
        s.FrameRounding = 5.0f;
        s.GrabRounding = 5.0f;
        s.PopupRounding = 6.0f;
        s.TabRounding = 5.0f;
        s.ScrollbarRounding = 6.0f;
        s.WindowBorderSize = 1.0f;
        s.WindowPadding = ImVec2(12, 10);
        s.FramePadding = ImVec2(8, 4);
        s.ItemSpacing = ImVec2(8, 6);
        s.GrabMinSize = 10.0f;
        s.FontSizeBase = 17.0f;
        s.FontScaleMain = uiScale;
        s.FontScaleDpi = dpi;
        io.FontDefault = fontUI;
        s.ScaleAllSizes(uiScale * dpi);
    }
}

// ------------------------------------------------------------------ small helpers
static void helpTip(const char* text) {
    if (!text || !*text) return;
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void paletteStrip(const Palette& p, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetCursorScreenPos();
    int n = 64;
    for (int i = 0; i < n; i++) {
        int k = i * 256 / n;
        ImU32 col = IM_COL32(p.rgba[k * 4], p.rgba[k * 4 + 1], p.rgba[k * 4 + 2], 255);
        dl->AddRectFilled(ImVec2(o.x + w * i / n, o.y), ImVec2(o.x + w * (i + 1) / n + 1, o.y + h), col);
    }
    ImGui::Dummy(ImVec2(w, h));
}

static bool paletteCombo(App& app) {
    bool changed = false;
    float stripW = ImGui::GetFontSize() * 5.0f, stripH = ImGui::GetFontSize() * 0.8f;
    if (ImGui::BeginCombo("Palette", app.palettes[app.rs.palette].name.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < (int)app.palettes.size(); i++) {
            ImGui::PushID(i);
            paletteStrip(app.palettes[i], stripW, stripH);
            ImGui::SameLine();
            if (ImGui::Selectable(app.palettes[i].name.c_str(), i == app.rs.palette)) {
                app.rs.palette = i;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    paletteStrip(app.palettes[app.rs.palette], ImGui::CalcItemWidth(), stripH);
    if (changed) app.applyPalette();
    return changed;
}

// Opens a file or folder with the desktop's default application. Uses
// posix_spawn with an argv (no shell), so any character in the path is safe.
static void openPath(const fs::path& p) {
    std::string path = p.string();
    char* argv[] = {(char*)"xdg-open", path.data(), nullptr};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    if (posix_spawnp(&pid, "xdg-open", &fa, nullptr, argv, environ) == 0)
        std::thread([pid] { waitpid(pid, nullptr, 0); }).detach();  // reap it, don't block the UI
    posix_spawn_file_actions_destroy(&fa);
}

// ------------------------------------------------------------------ top level
void App::drawUI() {
    if (showUI) {
        drawMenuBar();
        if (mode == ViewMode::Fractal3D) drawControlPanel();
        else drawClassicPanel();
        if (showLearn) drawLearnPanel();
        drawHud();
    }
    if (mode == ViewMode::Classic2D && cs.showOrbit) drawOrbitOverlay();
    if (showHelp) drawHelp();
    if (showPoster || poster.active) drawPosterDialog();
    if (showDemo) ImGui::ShowDemoWindow(&showDemo);
    drawToast();
}

void App::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
        ImGui::InputText("##parname", parName, sizeof parName);
        ImGui::SameLine();
        if (ImGui::Button("Save PAR")) saveNamedPar();
        helpTip("Saves everything about the current view (fractal, camera, colors, lighting) to a small text file. Ctrl+S.");
        if (ImGui::BeginMenu("Load PAR")) {
            auto files = listParFiles();
            if (files.empty()) ImGui::TextDisabled("(no .par files yet)");
            std::string lastDir;
            for (auto& f : files) {
                std::string dir = f.parent_path().filename().string() == "presets" ? "Built-in presets" : "Your saved views";
                if (dir != lastDir) {
                    ImGui::SeparatorText(dir.c_str());
                    lastDir = dir;
                }
                if (ImGui::MenuItem(f.stem().string().c_str())) loadPar(f);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Screenshot", "F12")) takeScreenshot();
        if (ImGui::MenuItem("Render high-res image...")) showPoster = true;
        if (ImGui::MenuItem("Open pictures folder")) {
            std::error_code ec;
            fs::create_directories(picturesDir, ec);
            openPath(picturesDir);
        }
        if (ImGui::MenuItem("Open saved views folder")) openPath(userDir / "params");
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Mode")) {
        if (ImGui::MenuItem("3D fractals", "M", mode == ViewMode::Fractal3D)) setMode(ViewMode::Fractal3D);
        if (ImGui::MenuItem("Classic 2D (Fractint style)", "M", mode == ViewMode::Classic2D)) setMode(ViewMode::Classic2D);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Fractal")) {
        std::string cat;
        for (int i = 0; i < (int)lib_.all().size(); i++) {
            auto& f = lib_.all()[i];
            if (f.category != cat) {
                cat = f.category;
                ImGui::SeparatorText(cat.c_str());
            }
            char key[16] = "";
            if (i < 9) snprintf(key, sizeof key, "%d", i + 1);
            if (ImGui::MenuItem(f.name.c_str(), key, mode == ViewMode::Fractal3D && i == current_)) {
                selectFractal(i, true);
                setMode(ViewMode::Fractal3D);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Controls & everything (Tab hides all)", "Tab", &showUI);
        ImGui::MenuItem("Learn panel", "L", &showLearn);
        ImGui::MenuItem("Keyboard & mouse help", "F1", &showHelp);
        if (ImGui::MenuItem("Fullscreen", "F11", fullscreen)) toggleFullscreen();
        int nMon = 0;
        GLFWmonitor** mons = glfwGetMonitors(&nMon);
        if (nMon > 1 && ImGui::BeginMenu("Fullscreen on")) {
            for (int i = 0; i < nMon; i++) {
                const char* name = glfwGetMonitorName(mons[i]);
                const GLFWvidmode* vm = glfwGetVideoMode(mons[i]);
                char label[160];
                snprintf(label, sizeof label, "%s (%dx%d)", name ? name : "monitor", vm ? vm->width : 0, vm ? vm->height : 0);
                if (ImGui::MenuItem(label, nullptr, fullscreenMonitor == (name ? name : ""))) {
                    fullscreenMonitor = name ? name : "";
                    if (fullscreen) toggleFullscreen();  // leave, then re-enter on the chosen monitor
                    toggleFullscreen();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::SeparatorText("Theme");
        if (ImGui::MenuItem("Modern", nullptr, uiTheme == 0)) uiTheme = 0, applyTheme();
        if (ImGui::MenuItem("Fractint (DOS blue)", nullptr, uiTheme == 1)) uiTheme = 1, applyTheme();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        if (ImGui::SliderFloat("UI scale", &uiScale, 0.75f, 2.0f, "%.2f")) applyTheme();
        ImGui::Separator();
        ImGui::MenuItem("ImGui demo (for the curious)", nullptr, &showDemo);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Keyboard & mouse", "F1", &showHelp);
        if (ImGui::MenuItem("Open the fractals folder (add your own!)")) openPath(dataDir / "fractals");
        ImGui::Separator();
        ImGui::TextDisabled("Fract3D - a 3D homage to Fractint");
        ImGui::TextDisabled("Every fractal is a .glsl file; edits reload live.");
        ImGui::EndMenu();
    }
    // right-aligned status
    char buf[160];
    if (mode == ViewMode::Fractal3D) {
        int maxS = rs.renderMode ? rs.maxSamplesPT : rs.maxSamplesRT;
        snprintf(buf, sizeof buf, "%s  |  %s  |  %d/%d samples  |  %.0f fps", fractal().name.c_str(),
                 rs.renderMode ? "path traced" : "real-time", samples, maxS, fps);
    } else {
        snprintf(buf, sizeof buf, "%s%s  |  zoom %.3gx  |  %.0f fps", kClassicFormulas[cs.formula], cs.julia ? " Julia" : "",
                 3.0 / cs.height, fps);
    }
    float w = ImGui::CalcTextSize(buf).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - w - ImGui::GetStyle().ItemSpacing.x * 2);
    ImGui::TextDisabled("%s", buf);
    ImGui::EndMainMenuBar();
}

// ------------------------------------------------------------------ 3D control panel
void App::drawControlPanel() {
    float fs_ = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(10, fs_ * 2.4f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(fs_ * 24, winH * 0.82f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Fract3D")) {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(-fs_ * 8.5f);
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Fractal")) { drawFractalTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Color")) { drawColorTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Light")) { drawLightTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Render")) { drawRenderTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Camera")) { drawCameraTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Post")) { drawPostTab(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

bool App::paramWidget(Param& p) {
    ImGui::PushID(p.id.c_str());
    bool changed = false;
    const char* lbl = p.label.c_str();
    switch (p.type) {
    case ParamType::Float:
        changed = ImGui::SliderFloat(lbl, &p.value[0], p.minV, p.maxV, p.logScale ? "%.4g" : "%.3f",
                                     p.logScale ? ImGuiSliderFlags_Logarithmic : 0);
        break;
    case ParamType::Int: {
        int v = (int)std::lround(p.value[0]);
        if ((changed = ImGui::SliderInt(lbl, &v, (int)p.minV, (int)p.maxV))) p.value[0] = (float)v;
        break;
    }
    case ParamType::Bool: {
        bool b = p.value[0] > 0.5f;
        if ((changed = ImGui::Checkbox(lbl, &b))) p.value[0] = b ? 1.0f : 0.0f;
        break;
    }
    case ParamType::Choice: {
        int v = (int)std::lround(p.value[0]);
        std::vector<const char*> items;
        for (auto& c : p.choices) items.push_back(c.c_str());
        if ((changed = ImGui::Combo(lbl, &v, items.data(), (int)items.size()))) p.value[0] = (float)v;
        break;
    }
    case ParamType::Vec2: changed = ImGui::SliderFloat2(lbl, p.value, p.minV, p.maxV, "%.3f"); break;
    case ParamType::Vec3: changed = ImGui::SliderFloat3(lbl, p.value, p.minV, p.maxV, "%.3f"); break;
    case ParamType::Vec4: changed = ImGui::SliderFloat4(lbl, p.value, p.minV, p.maxV, "%.3f"); break;
    case ParamType::Color: changed = ImGui::ColorEdit3(lbl, p.value); break;
    }
    helpTip(p.desc.c_str());
    if (ImGui::BeginPopupContextItem("ctx")) {
        if (ImGui::MenuItem("Reset to default")) p.reset(), changed = true;
        ImGui::EndPopup();
    }
    bool animatable = p.type == ParamType::Float || p.type == ParamType::Vec2 || p.type == ParamType::Vec3 ||
                      p.type == ParamType::Vec4 || p.type == ParamType::Color;
    if (animatable) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, p.animate ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                                                         : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        if (ImGui::SmallButton("~")) p.animate = !p.animate;
        ImGui::PopStyleColor();
        helpTip("Animate: sweep this value back and forth over time. Space pauses all animation.");
        if (p.animate) {
            ImGui::Indent();
            ImGui::SliderFloat("speed (Hz)", &p.animSpeed, 0.005f, 2.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("depth", &p.animDepth, 0.0f, 1.0f, "%.2f");
            ImGui::Unindent();
        }
    }
    ImGui::PopID();
    return changed;
}

void App::drawFractalTab() {
    Fractal& f = fractal();
    if (ImGui::BeginCombo("Fractal", f.name.c_str(), ImGuiComboFlags_HeightLarge)) {
        std::string cat;
        for (int i = 0; i < (int)lib_.all().size(); i++) {
            auto& o = lib_.all()[i];
            if (o.category != cat) {
                cat = o.category;
                ImGui::SeparatorText(cat.c_str());
            }
            if (ImGui::Selectable(o.name.c_str(), i == current_)) selectFractal(i, true);
        }
        ImGui::EndCombo();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", f.credit.c_str());
    ImGui::PopStyleColor();

    auto& progs = rend.programs(f);
    if (!rend.coreError.empty() || !progs.error.empty() || !f.parseError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.4f, 1));
        ImGui::TextWrapped("Shader problem - fix the file and save, it reloads automatically:");
        ImGui::PopStyleColor();
        std::string err = rend.coreError + f.parseError + progs.error;
        ImGui::PushFont(fontMono, 0.0f);
        ImGui::InputTextMultiline("##err", &err, ImVec2(-1, ImGui::GetFontSize() * 10), ImGuiInputTextFlags_ReadOnly);
        ImGui::PopFont();
    }

    if (ImGui::Button("Reset view")) resetView();
    ImGui::SameLine();
    if (ImGui::Button("Reset parameters"))
        for (auto& p : f.params) p.reset();
    ImGui::SameLine();
    if (ImGui::Button("Surprise me")) {
        static std::mt19937 rng{std::random_device{}()};
        std::normal_distribution<float> n(0.0f, 1.0f);
        for (auto& p : f.params) {
            if (p.type != ParamType::Float && p.type != ParamType::Vec3 && p.type != ParamType::Vec2 && p.type != ParamType::Vec4) continue;
            if (p.id == "zoom" || p.id == "center") continue;
            for (int k = 0; k < p.components(); k++)
                p.value[k] = std::clamp(p.value[k] + n(rng) * 0.08f * (p.maxV - p.minV), p.minV, p.maxV);
        }
    }
    helpTip("Nudge every shape parameter a little in a random direction. Great for discovering new forms.");
    if (ImGui::Button("Edit this fractal's .glsl")) openPath(f.path);
    helpTip("Opens the fractal's source in your editor. Save it and the change appears immediately.");
    if (f.key == "landscape") {
        ImGui::SameLine();
        if (ImGui::Button("Flatten to 2D")) flattenTo2D();
    }
    ImGui::SeparatorText("Parameters");
    for (auto& p : f.params) paramWidget(p);
    ImGui::Spacing();
    ImGui::TextDisabled("Hover a control for an explanation. Right-click resets it.");
}

void App::drawColorTab() {
    paletteCombo(*this);
    if (rs.palette == customPaletteIdx) {
        ImGui::SeparatorText("Cosine palette: a + b*cos(2pi(c*t + d))");
        bool ch = false;
        ch |= ImGui::SliderFloat3("a (offset)", customCosine.a, 0, 1);
        ch |= ImGui::SliderFloat3("b (amplitude)", customCosine.b, 0, 1);
        ch |= ImGui::SliderFloat3("c (frequency)", customCosine.c, 0, 3);
        ch |= ImGui::SliderFloat3("d (phase)", customCosine.d, 0, 1);
        if (ch) applyPalette();
        helpTip("Inigo Quilez's procedural palette: one cosine wave per color channel. Keep c whole numbers so the palette wraps smoothly.");
    }
    if (ImGui::Button("Save palette as .map")) {
        std::string name = palettes[rs.palette].name;
        for (auto& ch : name)
            if (!isalnum((unsigned char)ch)) ch = '_';
        fs::path p = userDir / "palettes" / (name + ".map");
        if (saveMapFile(p.string(), palettes[rs.palette])) toast("Saved " + p.string());
    }
    helpTip("Fractint .MAP format: 256 lines of 'r g b'. Put .map files in ~/.config/fract3d/palettes to load them at startup.");

    ImGui::SeparatorText("Coloring");
    const char* modes[] = {"Orbit trap A", "Orbit trap B", "Iteration count", "Surface normal", "Flat (base color)"};
    ImGui::Combo("Color source", &rs.colorMode, modes, 5);
    helpTip("What picks the palette color at each surface point.\n\nOrbit traps record how close the point's orbit came to some shape (a point, a plane) while iterating. They reveal the fractal's internal structure. Each fractal file decides what traps A and B measure.");
    ImGui::SliderFloat("Color scale", &rs.colorScale, 0.05f, 20.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    helpTip("How fast colors repeat. Higher values give more bands.");
    ImGui::SliderFloat("Color offset", &rs.colorOffset, 0.0f, 1.0f);
    ImGui::SliderFloat("Palette strength", &rs.paletteMix, 0.0f, 1.0f);
    helpTip("Blend between the palette and the plain base color. Set it low for a clay or plaster look, which works well with path tracing.");
    ImGui::ColorEdit3("Base color", rs.baseColor);
    ImGui::SliderFloat("Specular", &rs.specular, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &rs.roughness, 0.02f, 1.0f);

    ImGui::SeparatorText("Color cycling (C)");
    ImGui::SliderFloat("Cycle speed", &rs.cycleSpeed, -64.0f, 64.0f, "%.1f entries/s");
    helpTip("Fractint's famous party trick: rotate the palette so colors flow through the fractal. In 3D it re-renders every frame, so it looks best in real-time mode.");
}

void App::drawLightTab() {
    ImGui::Checkbox("Keep my lighting when switching fractals", &keepLighting);
    helpTip("Normally each fractal brings its own curated lighting. Tick this to keep your sun, sky, fog and glow instead.");
    ImGui::SeparatorText("Presets");
    if (ImGui::Button("Daylight")) {
        rs.sunAzimuth = 35; rs.sunElevation = 38; rs.sunIntensity = 2.6f; rs.sunSize = 2;
        float sc[3] = {1, 0.93f, 0.82f}, z[3] = {0.22f, 0.40f, 0.78f}, h[3] = {0.78f, 0.84f, 0.92f};
        std::copy(sc, sc + 3, rs.sunColor); std::copy(z, z + 3, rs.skyZenith); std::copy(h, h + 3, rs.skyHorizon);
        rs.skyIntensity = 0.9f; rs.background = 0; rs.glowStrength = 0; rs.fogDensity = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Golden hour")) {
        rs.sunAzimuth = 250; rs.sunElevation = 9; rs.sunIntensity = 3.2f; rs.sunSize = 1.5f;
        float sc[3] = {1, 0.62f, 0.32f}, z[3] = {0.16f, 0.20f, 0.42f}, h[3] = {0.95f, 0.62f, 0.42f};
        std::copy(sc, sc + 3, rs.sunColor); std::copy(z, z + 3, rs.skyZenith); std::copy(h, h + 3, rs.skyHorizon);
        rs.skyIntensity = 0.7f; rs.background = 0; rs.fogDensity = 0.08f;
        float fc[3] = {0.85f, 0.6f, 0.45f}; std::copy(fc, fc + 3, rs.fogColor);
    }
    ImGui::SameLine();
    if (ImGui::Button("Deep space glow")) {
        rs.background = 1; rs.bgColor[0] = 0.0f; rs.bgColor[1] = 0.0f; rs.bgColor[2] = 0.02f;
        rs.glowStrength = 0.8f; rs.skyIntensity = 0.25f; rs.sunIntensity = 1.8f; rs.fogDensity = 0;
        float g[3] = {0.25f, 0.5f, 1.0f}; std::copy(g, g + 3, rs.glowColor);
    }
    ImGui::SeparatorText("Sun");
    ImGui::SliderFloat("Azimuth", &rs.sunAzimuth, 0.0f, 360.0f, "%.0f deg");
    ImGui::SliderFloat("Elevation", &rs.sunElevation, -10.0f, 90.0f, "%.0f deg");
    ImGui::ColorEdit3("Sun color", rs.sunColor);
    ImGui::SliderFloat("Intensity", &rs.sunIntensity, 0.0f, 8.0f);
    ImGui::SliderFloat("Sun size", &rs.sunSize, 0.1f, 15.0f, "%.1f deg");
    helpTip("Angular radius of the sun. Bigger means softer shadows, like an overcast day.");
    ImGui::Checkbox("Shadows", &rs.shadows);
    ImGui::SeparatorText("Sky & ambient");
    const char* bgs[] = {"Sky gradient", "Solid color"};
    ImGui::Combo("Background", &rs.background, bgs, 2);
    if (rs.background == 0) {
        ImGui::ColorEdit3("Zenith", rs.skyZenith);
        ImGui::ColorEdit3("Horizon", rs.skyHorizon);
    } else {
        ImGui::ColorEdit3("Background color", rs.bgColor);
    }
    ImGui::SliderFloat("Sky light", &rs.skyIntensity, 0.0f, 3.0f);
    ImGui::SliderFloat("Ambient occlusion", &rs.aoStrength, 0.0f, 1.0f);
    helpTip("Darkens creases and cavities that ambient light can't reach easily. Path-traced mode computes this properly.");
    ImGui::SeparatorText("Atmosphere");
    ImGui::SliderFloat("Fog", &rs.fogDensity, 0.0f, 2.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::ColorEdit3("Fog color", rs.fogColor);
    ImGui::SliderFloat("Glow", &rs.glowStrength, 0.0f, 3.0f);
    helpTip("Adds light wherever rays took many steps, meaning they passed close to the surface. It's a cheap trick that makes fractals look electric.");
    ImGui::ColorEdit3("Glow color", rs.glowColor);
    ImGui::SeparatorText("Ground");
    ImGui::Checkbox("Ground plane", &rs.floorOn);
    helpTip("A flat floor under the fractal. It catches shadows and, when path traced, bounces light back up.");
    if (rs.floorOn) {
        ImGui::SliderFloat("Ground height", &rs.floorY, -20.0f, 5.0f, "%.3f");
        ImGui::ColorEdit3("Ground color", rs.floorColor);
    }
}

void App::drawRenderTab() {
    ImGui::SeparatorText("Mode (P)");
    ImGui::RadioButton("Real-time", &rs.renderMode, 0);
    helpTip("Direct sunlight, soft shadows and approximate ambient occlusion. Fast enough to fly around in.");
    ImGui::SameLine();
    ImGui::RadioButton("Path traced", &rs.renderMode, 1);
    helpTip("Physically based global illumination: light bounces between surfaces. Noisy while moving; hold still and it refines to a photographic image.");
    if (rs.renderMode == 1) ImGui::SliderInt("Bounces", &rs.bounces, 1, 8);

    ImGui::SeparatorText("Ray marching");
    ImGui::SliderInt("Max steps", &rs.maxSteps, 32, 2000, "%d", ImGuiSliderFlags_Logarithmic);
    helpTip("Most steps a ray may take before giving up. Raise it if you see holes or dark noise at grazing angles.");
    ImGui::SliderFloat("Detail", &rs.detail, 0.02f, 4.0f, "%.3f px", ImGuiSliderFlags_Logarithmic);
    helpTip("A ray stops when it's this close to the surface, measured in pixels. Smaller values give finer detail and cost more.");
    ImGui::SliderFloat("Step factor", &rs.stepFactor, 0.05f, 1.0f, "%.2f");
    helpTip("Multiplies each step. Distance estimates for some fractals are a little optimistic; smaller steps prevent overshooting through thin features (at a speed cost).");
    ImGui::SliderFloat("Max distance", &rs.maxDist, 1.0f, 100.0f, "%.1f", ImGuiSliderFlags_Logarithmic);

    ImGui::SeparatorText("Refinement");
    ImGui::SliderInt("Samples (real-time)", &rs.maxSamplesRT, 1, 512, "%d", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderInt("Samples (path traced)", &rs.maxSamplesPT, 1, 65536, "%d", ImGuiSliderFlags_Logarithmic);
    helpTip("While the view is still, jittered samples are averaged for anti-aliasing, soft shadows, depth of field and global illumination.");
    int maxS = rs.renderMode ? rs.maxSamplesPT : rs.maxSamplesRT;
    ImGui::ProgressBar((float)samples / maxS, ImVec2(-1, 0), (std::to_string(samples) + " / " + std::to_string(maxS)).c_str());
    if (ImGui::Button("Restart")) lastSig3D.clear();

    ImGui::SeparatorText("Performance");
    ImGui::Checkbox("Adaptive resolution while moving", &rs.adaptiveRes);
    ImGui::SliderFloat("Target fps", &rs.targetFps, 20.0f, 240.0f, "%.0f");
    ImGui::SliderFloat("Render scale", &rs.stillScale, 0.25f, 2.0f, "%.2fx");
    helpTip("Resolution of the still image relative to the window. Above 1 supersamples.");
    ImGui::TextDisabled("GPU: %.2f ms per full-res sample | motion scale %.2f", perSampleMsFull, motionScale);
    ImGui::TextDisabled("Buffer %d x %d", rend.accum.w, rend.accum.h);
}

void App::drawCameraTab() {
    ImGui::SliderFloat("Field of view", &rs.fov, 10.0f, 120.0f, "%.0f deg");
    ImGui::SliderFloat("Fly speed", &flySpeed, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    helpTip("WASD speed, measured in 'distances to the nearest surface per second'. So flying slows down automatically near surfaces and speeds up in open space.");
    ImGui::SeparatorText("Depth of field");
    ImGui::SliderFloat("Aperture", &rs.aperture, 0.0f, 2.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    helpTip("Lens size. Bigger gives a blurrier background and a stronger macro-photo look. Takes a few samples to resolve.");
    ImGui::Checkbox("Autofocus on screen center", &rs.autoFocus);
    if (!rs.autoFocus) ImGui::SliderFloat("Focus distance", &rs.focusDist, 1e-4f, 100.0f, "%.4g", ImGuiSliderFlags_Logarithmic);
    ImGui::TextDisabled("Press F to focus on whatever is at screen center.");
    ImGui::SeparatorText("Position");
    ImGui::InputFloat3("Position", &cam.pos.x, "%.5f");
    float ang[2] = {cam.yaw * 57.29578f, cam.pitch * 57.29578f};
    if (ImGui::InputFloat2("Yaw / pitch", ang, "%.2f")) cam.yaw = ang[0] / 57.29578f, cam.pitch = ang[1] / 57.29578f;
    ImGui::InputFloat("Orbit distance", &cam.distance, 0, 0, "%.5g");
    ImGui::TextDisabled("Distance to surface: %.3g", deAtCam);
    if (ImGui::Button("Reset view (R)")) resetView();
}

void App::drawPostTab() {
    ImGui::SliderFloat("Exposure", &rs.exposure, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    const char* tm[] = {"ACES filmic", "Reinhard", "None (clip)"};
    ImGui::Combo("Tone mapping", &rs.tonemap, tm, 3);
    ImGui::SliderFloat("Saturation", &rs.saturation, 0.0f, 2.0f);
    ImGui::SliderFloat("Vignette", &rs.vignette, 0.0f, 1.0f);
    ImGui::SeparatorText("Retro");
    const char* rm[] = {"Off", "VGA 256 colors", "EGA 16 colors"};
    ImGui::Combo("Palette quantize", &rs.retro, rm, 3);
    helpTip("Snap every pixel to the 1987 VGA palette (or the 16 EGA colors), with ordered dithering like old games used.");
    ImGui::SliderInt("Pixel size", &rs.pixelSize, 1, 8);
    ImGui::SliderFloat("Scanlines", &rs.scanlines, 0.0f, 0.8f);
    if (ImGui::Button("1990 mode")) {
        rs.retro = 1; rs.pixelSize = 3; rs.scanlines = 0.25f;
        for (int i = 0; i < (int)palettes.size(); i++)
            if (palettes[i].name.rfind("Fractint VGA", 0) == 0) rs.palette = i;
        applyPalette();
        uiTheme = 1;
        applyTheme();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back to modern")) {
        rs.retro = 0; rs.pixelSize = 1; rs.scanlines = 0;
        uiTheme = 0;
        applyTheme();
    }
}

// ------------------------------------------------------------------ classic 2D panel
void App::drawClassicPanel() {
    float fs_ = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(10, fs_ * 2.4f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(fs_ * 24, winH * 0.82f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Classic 2D")) {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(-fs_ * 8.5f);
    ImGui::Combo("Formula", &cs.formula, kClassicFormulas, kClassicFormulaCount);
    if (cs.formula != 4) {
        ImGui::Checkbox("Julia set", &cs.julia);
        helpTip("Shortcut: right-click (or press Space) on any point of the Mandelbrot set to see the Julia set for that c. Do it again to go back.");
        if (cs.julia) {
            ImGui::InputDouble("c real", &cs.jx, 0.001, 0.01, "%.10f");
            ImGui::InputDouble("c imag", &cs.jy, 0.001, 0.01, "%.10f");
        }
    }
    if (cs.formula == 3) ImGui::SliderInt("Power", &cs.power, 2, 8);
    if (cs.formula == 5) ImGui::SliderFloat2("Phoenix p", cs.phoenixP, -1.0f, 1.0f);

    ImGui::SeparatorText("View");
    ImGui::InputDouble("Center real", &cs.cx, 0, 0, "%.15f");
    ImGui::InputDouble("Center imag", &cs.cy, 0, 0, "%.15f");
    double mag = 3.0 / cs.height;
    if (ImGui::InputDouble("Magnification", &mag, 0, 0, "%.6g") && mag > 0) cs.height = 3.0 / mag;
    bool fp64 = rend.classicUsesFp64(cs, fbH);
    const char* prec[] = {"Single (fast)", "Double", "Auto"};
    ImGui::Combo("Precision", &cs.fp64, prec, 3);
    helpTip("32-bit floats have about 7 significant digits, so past roughly 100,000x zoom the image turns blocky. Doubles give about 16 digits (deep zooms to around 10^13x) at lower speed. Fractint used clever integer math for the same reason, hence the 'int' in its name.");
    ImGui::SameLine();
    ImGui::TextDisabled(fp64 ? "(fp64)" : "(fp32)");
    if (ImGui::Button("Reset view (Home)")) {
        cs.julia = 0;
        cs.cx = cs.formula == 4 ? 0.0 : -0.6;
        cs.cy = 0;
        cs.height = 3.0;
    }
    ImGui::SameLine();
    bool liftable = cs.formula <= 3 && !(cs.julia && cs.formula != 0) && !(cs.formula == 3 && cs.power != 3);
    ImGui::BeginDisabled(!liftable);
    if (ImGui::Button("Lift into 3D landscape")) liftTo3D();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(liftable ? "Turn the escape time into height and fly over the result in 3D."
                                   : "The 3D landscape supports Mandelbrot (and its Julia sets), Burning Ship, Tricorn and "
                                     "Multibrot z^3. Switch to one of those to lift this view into 3D.");

    ImGui::SeparatorText("Iteration");
    ImGui::SliderInt("Max iterations", &cs.maxIter, 16, kMaxIterations, "%d", ImGuiSliderFlags_Logarithmic);
    if (job2D.active) {
        float th = (float)(job2D.offscreen ? work2D.h : rend.index2D.h);
        float bandFrac = job2D.bandRows > 0 ? (float)job2D.itersDone / std::max(job2D.cs.maxIter, 1) : 0.0f;
        float prog = th > 0 ? (job2D.row + job2D.bandRows * bandFrac) / th : 0.0f;
        ImGui::ProgressBar(prog, ImVec2(-1, 0), "drawing...");
    }
    helpTip("Points still bounded after this many steps are declared 'inside'. Deeper zooms need more. Keys: + and - double or halve it.");
    ImGui::SliderFloat("Bailout", &cs.bailout, 2.0f, 1000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    helpTip("Escape radius: once |z| passes it, the point is counted as escaped. 2 is Fractint's value and is enough to prove escape for the Mandelbrot set. It changes the color bands, not the set itself. Smooth coloring always uses at least 64.");

    ImGui::SeparatorText("Color");
    paletteCombo(*this);
    ImGui::Checkbox("Fractint bands (B)", &cs.banded);
    helpTip("On: one palette entry per whole iteration, the classic 1990 look. Off: the smooth (continuous) iteration count blends the colors.");
    ImGui::SliderFloat("Color density", &cs.colorDensity, 0.05f, 16.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    const char* ins[] = {"Black", "zmag (Fractint)", "Solid color"};
    ImGui::Combo("Inside", &cs.insideMode, ins, 3);
    if (cs.insideMode == 2) ImGui::ColorEdit3("Inside color", cs.insideColor);
    if (cs.formula == 4) ImGui::SliderFloat("Root color spread", &cs.rootSpread, 0.0f, 128.0f);
    ImGui::SliderFloat("Cycle speed (C)", &rs.cycleSpeed, -64.0f, 64.0f, "%.1f");
    helpTip("Palette rotation. Costs nothing here: the iteration counts are stored and only the colors are looked up again, just like Fractint rotating the VGA hardware palette.");
    ImGui::SliderInt("Anti-aliasing", &cs.supersample, 1, 4, "%d x");
    helpTip("Computes N x N points per pixel and averages their colors. 2 is a good balance.");
    ImGui::Checkbox("Show orbit under cursor (O)", &cs.showOrbit);
    helpTip("Draws the sequence z0, z1, z2... for the point under your mouse. This is literally what's computed for every pixel.");
    ImGui::SeparatorText("Retro");
    const char* rm[] = {"Off", "VGA 256 colors", "EGA 16 colors"};
    ImGui::Combo("Quantize", &rs.retro, rm, 3);
    ImGui::SliderInt("Pixel size", &rs.pixelSize, 1, 8);
    ImGui::SliderFloat("Scanlines", &rs.scanlines, 0.0f, 0.8f);
    ImGui::PopItemWidth();
    ImGui::Spacing();
    ImGui::TextDisabled("Drag: pan  |  Wheel/PgUp/PgDn: zoom\nRight-click / Space: Julia <-> Mandelbrot");
    ImGui::End();
}

// ------------------------------------------------------------------ learn panel
void App::drawLesson(const std::string& text) {
    std::istringstream is(text);
    std::string line;
    float fs_ = ImGui::GetStyle().FontSizeBase;
    const ImVec4 accent = uiTheme == 1 ? ImVec4(1, 1, 0.33f, 1) : ImVec4(0.55f, 0.72f, 1.0f, 1);
    const ImVec4 formula = uiTheme == 1 ? ImVec4(0.33f, 1, 1, 1) : ImVec4(0.95f, 0.80f, 0.55f, 1);
    while (std::getline(is, line)) {
        if (line.empty()) {
            ImGui::Spacing();
        } else if (line.rfind("# ", 0) == 0) {
            ImGui::Spacing();
            ImGui::PushFont(nullptr, fs_ * 1.25f);
            ImGui::PushStyleColor(ImGuiCol_Text, accent);
            ImGui::TextWrapped("%s", line.c_str() + 2);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        } else if (line.rfind("$ ", 0) == 0) {
            ImGui::PushFont(fontMono, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, formula);
            ImGui::Indent();
            ImGui::TextWrapped("%s", line.c_str() + 2);
            ImGui::Unindent();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        } else if (line.rfind("* ", 0) == 0) {
            ImGui::Bullet();
            ImGui::TextWrapped("%s", line.c_str() + 2);
        } else {
            ImGui::TextWrapped("%s", line.c_str());
        }
    }
}

// Splits "@@ Title" sections.
static std::vector<std::pair<std::string, std::string>> sections(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream is(text);
    std::string line;
    while (std::getline(is, line)) {
        if (line.rfind("@@ ", 0) == 0) out.push_back({line.substr(3), ""});
        else if (!out.empty()) out.back().second += line + "\n";
    }
    return out;
}

void App::drawLearnPanel() {
    float fs_ = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(winW - fs_ * 27 - 10, fs_ * 2.4f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(fs_ * 27, winH * 0.82f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Learn", &showLearn)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("learn")) {
        if (ImGui::BeginTabItem(mode == ViewMode::Fractal3D ? "This fractal" : "This formula")) {
            ImGui::BeginChild("lesson");
            if (mode == ViewMode::Fractal3D) {
                Fractal& f = fractal();
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
                ImGui::TextWrapped("%s", f.name.c_str());
                ImGui::PopFont();
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", f.credit.c_str());
                ImGui::PopStyleColor();
                ImGui::Separator();
                drawLesson(f.about);
            } else {
                static std::string classic = readTextFile((dataDir / "docs/classic.txt").string());
                bool found = false;
                for (auto& [title, body] : sections(classic))
                    if (title == kClassicFormulas[cs.formula]) {
                        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
                        ImGui::TextWrapped("%s", title.c_str());
                        ImGui::PopFont();
                        ImGui::Separator();
                        drawLesson(body);
                        found = true;
                    }
                if (!found) ImGui::TextDisabled("(no notes for this formula yet)");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Tour")) {
            ImGui::BeginChild("tour");
            ImGui::TextWrapped("A guided walk through the app. Each stop loads a saved view (a .par file). Press N for the next stop, Shift+N to go back.");
            if (ImGui::Button("< Previous")) tourStep(-1);
            ImGui::SameLine();
            if (ImGui::Button("Next >")) tourStep(1);
            ImGui::Separator();
            static std::vector<std::pair<fs::path, std::string>> stops;
            if (stops.empty()) {
                std::vector<fs::path> files;
                std::error_code ec;
                for (auto& e : fs::directory_iterator(dataDir / "presets", ec))
                    if (e.path().extension() == ".par") files.push_back(e.path());
                std::sort(files.begin(), files.end());
                for (auto& f : files) {
                    std::string first;
                    std::istringstream is(readTextFile(f.string()));
                    std::getline(is, first);
                    stops.push_back({f, first.rfind(";", 0) == 0 ? first.substr(first.find_first_not_of("; ")) : ""});
                }
            }
            for (int i = 0; i < (int)stops.size(); i++) {
                std::string title = stops[i].first.stem().string();
                for (auto& ch : title)
                    if (ch == '-') ch = ' ';
                ImGui::PushID(i);
                if (ImGui::Selectable(title.c_str(), i == tourIdx)) {
                    tourIdx = i - 1;
                    tourStep(1);
                }
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::Indent();
                ImGui::TextWrapped("%s", stops[i].second.c_str());
                ImGui::Unindent();
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Concepts")) {
            ImGui::BeginChild("concepts");
            for (auto& [title, body] : sections(conceptsText)) {
                if (ImGui::CollapsingHeader(title.c_str())) {
                    ImGui::Indent(fs_ * 0.4f);
                    drawLesson(body);
                    ImGui::Unindent(fs_ * 0.4f);
                    ImGui::Spacing();
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ------------------------------------------------------------------ HUD, help, toast
void App::drawHud() {
    ImGuiIO& io = ImGui::GetIO();
    float pad = 10.0f;
    ImGui::SetNextWindowPos(ImVec2(pad, io.DisplaySize.y - pad), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowBgAlpha(0.45f);
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##hud", nullptr, fl)) {
        if (mode == ViewMode::Fractal3D) {
            ImGui::Text("%.0f fps  |  %d x %d  |  scale %.2f  |  %s", fps, rend.accum.w, rend.accum.h,
                        (float)rend.accum.w / std::max(fbW, 1), interactive ? "moving" : "refining");
            ImGui::TextDisabled("F1 help  |  Tab hide UI  |  F12 screenshot  |  P path trace  |  M 2D mode");
        } else {
            float sx = (float)fbW / std::max(winW, 1), sy = (float)fbH / std::max(winH, 1);
            double ps = cs.height / fbH;
            char where[96] = "cursor -";
            if (ImGui::IsMousePosValid()) {
                double px = cs.cx + (io.MousePos.x * sx - fbW * 0.5) * ps, py = cs.cy + (fbH * 0.5 - io.MousePos.y * sy) * ps;
                snprintf(where, sizeof where, "cursor %.12f %+.12fi", px, py);
            }
            ImGui::Text("%s  |  zoom %.4gx  |  %d iter  |  %s", where, 3.0 / cs.height, cs.maxIter,
                        rend.classicUsesFp64(cs, fbH) ? "fp64" : "fp32");
            ImGui::TextDisabled("F1 help  |  Tab hide UI  |  O orbits  |  C color cycling  |  M 3D mode");
        }
    }
    ImGui::End();
}

void App::drawHelp() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Keyboard & mouse", &showHelp, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    auto table = [](const char* id, std::initializer_list<std::pair<const char*, const char*>> rows) {
        if (ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            for (auto& [k, v] : rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(k);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", v);
            }
            ImGui::EndTable();
        }
    };
    ImGui::SeparatorText("3D");
    table("h3d", {{"Left drag", "orbit around the target"},
                  {"Right drag", "look around (turn in place)"},
                  {"Middle drag / Shift+left drag", "pan"},
                  {"Mouse wheel", "zoom toward the target"},
                  {"W A S D  /  arrows", "fly (speed adapts to the surface distance)"},
                  {"Q / E", "down / up"},
                  {"Shift", "fly 4x faster"},
                  {"Double-click", "orbit around the point you clicked"},
                  {"F", "focus depth of field at screen center"},
                  {"R", "reset view"},
                  {"P", "toggle path tracing"},
                  {"Space", "pause parameter animation"},
                  {"1 - 9", "switch fractal"}});
    ImGui::SeparatorText("Classic 2D");
    table("h2d", {{"Left drag", "pan"},
                  {"Wheel / PgUp / PgDn", "zoom at the cursor"},
                  {"Right-click / Space", "Julia set of the point under the cursor (and back)"},
                  {"O", "show the orbit of the point under the cursor"},
                  {"B", "toggle Fractint bands / smooth color"},
                  {"+ / -", "double / halve max iterations"},
                  {"Home", "reset view"}});
    ImGui::SeparatorText("Everywhere");
    table("hall", {{"Tab", "hide or show all UI"},
                   {"M", "switch between 3D and Classic 2D"},
                   {"C", "color cycling on/off"},
                   {"L", "Learn panel"},
                   {"N / Shift+N", "next / previous stop on the guided tour"},
                   {"F11", "fullscreen"},
                   {"F12", "screenshot (+ .par file to recreate it)"},
                   {"Ctrl+S", "save the current view as a .par"},
                   {"Ctrl+Q", "quit"}});
    ImGui::End();
}

void App::drawToast() {
    if (toastMsg.empty() || glfwGetTime() > toastUntil) return;
    ImGuiIO& io = ImGui::GetIO();
    float alpha = std::min(1.0f, (float)(toastUntil - glfwGetTime()) * 2.0f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 60), ImGuiCond_Always, ImVec2(0.5f, 1));
    ImGui::SetNextWindowBgAlpha(0.8f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::Begin("##toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::TextUnformatted(toastMsg.c_str());
    ImGui::End();
    ImGui::PopStyleVar();
}

void App::drawPosterDialog() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.4f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool open = true;
    if (!ImGui::Begin("Render high-res image", poster.active ? nullptr : &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (!open) showPoster = false;
    if (poster.active) {
        float prog;
        if (poster.mode == ViewMode::Fractal3D) {
            int tilesX = (poster.target.w + 1023) / 1024, tilesY = (poster.target.h + 1023) / 1024;
            prog = (poster.done * tilesX * tilesY + poster.tile) / (float)(poster.samples * tilesX * tilesY);
        } else {
            prog = poster.index.h > 0 ? (float)poster.job.row / poster.index.h : 0.0f;
        }
        ImGui::Text("Rendering %d x %d, %d samples...", poster.w, poster.h, poster.samples);
        ImGui::ProgressBar(prog, ImVec2(ImGui::GetFontSize() * 20, 0));
        double el = glfwGetTime() - poster.started;
        if (prog > 0.01f) ImGui::TextDisabled("%.0fs elapsed, about %.0fs left", el, el / prog - el);
        if (ImGui::Button("Cancel")) {
            poster.active = false;
            poster.target.release();
            poster.index.release();
            lastSig3D.clear();
            shownSig2D.clear();
        }
    } else {
        struct Preset { const char* name; int w, h; };
        const Preset presets[] = {{"1080p", 1920, 1080}, {"1440p", 2560, 1440}, {"4K", 3840, 2160},
                                  {"5K ultrawide", 5120, 1440}, {"8K", 7680, 4320}, {"Square 4K", 4096, 4096}};
        for (auto& p : presets) {
            if (ImGui::Button(p.name)) posterW = p.w, posterH = p.h;
            ImGui::SameLine();
        }
        if (ImGui::Button("Window x2")) posterW = fbW * 2, posterH = fbH * 2;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
        ImGui::InputInt("Width", &posterW, 0);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
        ImGui::InputInt("Height", &posterH, 0);
        posterW = std::clamp(posterW, 16, 16384);
        posterH = std::clamp(posterH, 16, 16384);
        if (mode == ViewMode::Fractal3D) {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
            ImGui::SliderInt("Samples", &posterSamples, 1, 8192, "%d", ImGuiSliderFlags_Logarithmic);
            ImGui::TextDisabled(rs.renderMode ? "Path traced: 256-2048 samples look great." : "Real-time mode: 16-64 samples is plenty.");
            ImGui::TextDisabled("Tip: aspect ratio differs from the window? The view is centered the same way.");
        }
        if (ImGui::Button("Render")) {
            startPoster(posterW, posterH, posterSamples);
            showPoster = false;
        }
    }
    ImGui::End();
}

// ------------------------------------------------------------------ orbit overlay (2D)
// Mirrors classic2d.frag on the CPU in double precision.
static int computeOrbit(const Classic2DSettings& cs, double px, double py, std::vector<std::complex<double>>& pts) {
    using C = std::complex<double>;
    C pixel(px, py);
    C c = cs.julia ? C(cs.jx, cs.jy) : pixel;
    C z = cs.julia ? pixel : C(0, 0);
    if (cs.formula == 1 && !cs.julia) c = std::conj(c);
    if (cs.formula == 6 && !cs.julia) z = C(0.5, 0);
    if (cs.formula == 4) z = pixel;
    C zprev(0, 0);
    double bail = std::max((double)cs.bailout, 2.0);
    int n = std::min(cs.maxIter, 2000);
    pts.clear();
    pts.push_back(z);
    for (int i = 0; i < n; i++) {
        switch (cs.formula) {
        case 0: z = z * z + c; break;
        case 1: z = C(std::abs(z.real()), std::abs(z.imag())); z = z * z + c; break;
        case 2: z = std::conj(z); z = z * z + c; break;
        case 3: { C r = z; for (int k = 1; k < cs.power; k++) r *= z; z = r + c; break; }
        case 4: {
            C f = z * z * z - 1.0;
            if (std::norm(f) < 1e-10) return i;
            z -= f / (3.0 * z * z);
            break;
        }
        case 5: { C zn = z * z + c.real() + C(cs.phoenixP[0], cs.phoenixP[1]) * zprev + C(0, c.imag()); zprev = z; z = zn; break; }
        case 6: z = c * z * (1.0 - z); break;
        case 7: { z = (z * z + c - 1.0) / (2.0 * z + c - 2.0); z = z * z; if (std::norm(z - 1.0) < 1e-8) return i; break; }
        }
        pts.push_back(z);
        if (cs.formula != 4 && std::norm(z) > bail * bail) return i + 1;
    }
    return -1;
}

void App::drawOrbitOverlay() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse || !ImGui::IsMousePosValid()) return;
    float sx = (float)fbW / std::max(winW, 1), sy = (float)fbH / std::max(winH, 1);
    double ps = cs.height / fbH;
    double px = cs.cx + (io.MousePos.x * sx - fbW * 0.5) * ps, py = cs.cy + (fbH * 0.5 - io.MousePos.y * sy) * ps;
    std::vector<std::complex<double>> pts;
    int esc = computeOrbit(cs, px, py, pts);
    bool flip = cs.formula == 1 && !cs.julia;
    auto toScreen = [&](std::complex<double> z) {
        double zy = flip ? -z.imag() : z.imag();
        return ImVec2((float)(((z.real() - cs.cx) / ps + fbW * 0.5) / sx), (float)((fbH * 0.5 - (zy - cs.cy) / ps) / sy));
    };
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    int n = (int)pts.size();
    for (int i = 1; i < n; i++) {
        float t = (float)i / n;
        ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f - t * 0.7f, 0.2f + t * 0.6f, 0.85f));
        ImVec2 a = toScreen(pts[i - 1]), b = toScreen(pts[i]);
        if (std::abs(a.x) > 1e5 || std::abs(a.y) > 1e5 || std::abs(b.x) > 1e5 || std::abs(b.y) > 1e5) break;
        dl->AddLine(a, b, col, 1.5f);
        dl->AddCircleFilled(b, 2.5f, col);
    }
    dl->AddCircle(toScreen(pts[0]), 6.0f, IM_COL32(255, 255, 255, 230), 0, 2.0f);
    // the escape circle |z| = bailout (in view if zoomed out)
    ImVec2 o = toScreen({0, 0});
    float r = (float)(std::max((double)cs.bailout, 2.0) / ps / sx);
    if (r < 1e5f) dl->AddCircle(o, r, IM_COL32(255, 255, 255, 60), 128, 1.0f);

    char buf[160];
    if (esc < 0) snprintf(buf, sizeof buf, "bounded after %d steps: inside (black)", (int)pts.size() - 1);
    else if (cs.formula == 4 || cs.formula == 7) snprintf(buf, sizeof buf, "converged to a root after %d steps", esc);
    else snprintf(buf, sizeof buf, "escaped after %d steps", esc);
    ImVec2 m(io.MousePos.x + 16, io.MousePos.y + 12);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    dl->AddRectFilled(ImVec2(m.x - 4, m.y - 2), ImVec2(m.x + ts.x + 4, m.y + ts.y + 2), IM_COL32(0, 0, 0, 170), 4.0f);
    dl->AddText(m, IM_COL32(255, 255, 255, 255), buf);
}
