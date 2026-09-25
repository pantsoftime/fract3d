#include "app.h"
#include "sanitize.h"
#include "settings.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::string timestampName() {
    std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", std::localtime(&t));
    return buf;
}

static fs::path findDataDir() {
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec).parent_path();
    for (fs::path cand : {exe, exe / "..", exe / "../share/fract3d", fs::path(FRACT3D_SOURCE_DIR)})
        if (fs::exists(cand / "shaders/raymarch.frag", ec) && fs::exists(cand / "fractals", ec))
            return fs::weakly_canonical(cand, ec);
    return fs::path(FRACT3D_SOURCE_DIR);
}

static fs::path homeDir() {
    const char* h = std::getenv("HOME");
    return h ? fs::path(h) : fs::path(".");
}

// ------------------------------------------------------------------ init
bool App::init(const CliOptions& opts) {
    cli = opts;
    dataDir = findDataDir();
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    userDir = (xdg && *xdg ? fs::path(xdg) : homeDir() / ".config") / "fract3d";
    picturesDir = homeDir() / "Pictures" / "fract3d";
    std::error_code ec;
    fs::create_directories(userDir / "params", ec);
    fs::create_directories(userDir / "palettes", ec);

    glfwSetErrorCallback([](int code, const char* msg) { fprintf(stderr, "glfw error %d: %s\n", code, msg); });
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#ifndef NDEBUG
    cli.glDebug = true;  // debug builds always report GL errors
#endif
    if (cli.glDebug) glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    if (cli.hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    int ww = cli.hidden && cli.shotW ? cli.shotW : 1600, wh = cli.hidden && cli.shotH ? cli.shotH : 900;
    win = glfwCreateWindow(ww, wh, "Fract3D", nullptr, nullptr);
    if (!win) {
        fprintf(stderr, "fract3d: could not create an OpenGL 4.6 window\n");
        return false;
    }
    glfwSetWindowUserPointer(win, this);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwGetFramebufferSize(win, &fbW, &fbH);
    glfwGetWindowSize(win, &winW, &winH);

    printf("fract3d: %s | %s\n", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    printf("fract3d: data from %s\n", dataDir.c_str());
    if (cli.glDebug) installGlDebugOutput(cli.glDebugVerbose);

    std::string err;
    if (!rend.init(dataDir, err)) {
        fprintf(stderr, "fract3d: shader setup failed:\n%s\n", err.c_str());
        return false;
    }
    lib_.load(dataDir / "fractals");
    if (lib_.all().empty()) {
        fprintf(stderr, "fract3d: no fractals found in %s/fractals\n", dataDir.c_str());
        return false;
    }
    rend.warmUp(lib_.all());  // start compiling every fractal in the background

    palettes = builtinPalettes();
    for (auto dir : {dataDir / "palettes", userDir / "palettes"}) {
        std::vector<fs::path> maps;
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".map") maps.push_back(e.path());
        std::sort(maps.begin(), maps.end());
        for (auto& m : maps) {
            Palette p;
            if (loadMapFile(m.string(), p)) {
                p.name = "MAP: " + p.name;
                palettes.push_back(p);
            }
        }
    }
    customPaletteIdx = (int)palettes.size();
    palettes.push_back(makeCosinePalette("Custom (cosine editor)", customCosine));

    conceptsText = readTextFile((dataDir / "docs/concepts.txt").string());

    loadPrefs();
    if (cli.theme >= 0) uiTheme = cli.theme;
    setupImGui();

    int idx = cli.fractal.empty() ? 0 : std::max(lib_.indexOf(cli.fractal), 0);
    selectFractal(idx, true);
    if (!cli.parFile.empty() && !loadPar(cli.parFile))
        fprintf(stderr, "fract3d: could not load %s\n", cli.parFile.c_str());
    if (cli.mode2d) mode = ViewMode::Classic2D;
    if (cli.pathTrace >= 0) rs.renderMode = cli.pathTrace;
    sanitize(rs);
    sanitize(cs);
    applyPalette();

    if (!cli.shotPath.empty()) {
        int w = cli.shotW ? cli.shotW : fbW, h = cli.shotH ? cli.shotH : fbH;
        int s = cli.shotSamples ? cli.shotSamples : (rs.renderMode ? 256 : 32);
        startPoster(w, h, s);
        poster.path = cli.shotPath;
    }
    lastFrameTime = glfwGetTime();
    return true;
}

void App::shutdown() {
    if (!cli.hidden) savePrefs();
    poster.target.release();
    rend.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (win) glfwDestroyWindow(win);
    glfwTerminate();
}

int App::run() {
    while (!glfwWindowShouldClose(win) && !quit) frame();
    return 0;
}

// ------------------------------------------------------------------ preferences
// UI preferences (not part of a view, so not in PAR files).
void App::loadPrefs() {
    std::istringstream is(readTextFile((userDir / "prefs.ini").string()));
    std::string line;
    while (std::getline(is, line)) {
        size_t e = line.find('=');
        if (e == std::string::npos) continue;
        std::string k = line.substr(0, e);
        float v = std::strtof(line.c_str() + e + 1, nullptr);
        if (k == "theme") uiTheme = (int)v;
        else if (k == "uiScale") uiScale = std::clamp(v, 0.5f, 3.0f);
        else if (k == "showLearn") showLearn = v != 0;
        else if (k == "flySpeed") flySpeed = std::isfinite(v) ? std::clamp(v, 0.01f, 100.0f) : 1.5f;
        else if (k == "keepLighting") keepLighting = v != 0;
        else
            visitRender(rs, [&](const char* name, const char*, auto* ptr, int, unsigned flags) {
                if ((flags & kPerf) && k == name) *ptr = static_cast<std::remove_reference_t<decltype(*ptr)>>(v);
            });
    }
    if (uiTheme != 0 && uiTheme != 1) uiTheme = 0;
    if (!std::isfinite(uiScale)) uiScale = 1.0f;
}

void App::savePrefs() {
    FILE* f = fopen((userDir / "prefs.ini").c_str(), "w");
    if (!f) return;
    fprintf(f, "theme=%d\nuiScale=%g\nshowLearn=%d\nflySpeed=%g\nkeepLighting=%d\n", uiTheme, uiScale, (int)showLearn,
            flySpeed, (int)keepLighting);
    visitRender(rs, [&](const char* name, const char*, auto* ptr, int, unsigned flags) {
        if (flags & kPerf) fprintf(f, "%s=%g\n", name, (double)*ptr);
    });
    fclose(f);
}

// ------------------------------------------------------------------ helpers
void App::toast(const std::string& msg, float seconds) {
    toastMsg = msg;
    toastUntil = glfwGetTime() + seconds;
    printf("fract3d: %s\n", msg.c_str());
}

void App::applyPalette() {
    rs.palette = std::clamp(rs.palette, 0, (int)palettes.size() - 1);
    if (rs.palette == customPaletteIdx)
        palettes[customPaletteIdx] = makeCosinePalette("Custom (cosine editor)", customCosine);
    rend.setPalette(palettes[rs.palette]);
    paletteVersion++;
}

void App::selectFractal(int idx, bool reset) {
    current_ = std::clamp(idx, 0, (int)lib_.all().size() - 1);
    Fractal& f = fractal();
    rs.stepFactor = f.hints.stepFactor;
    rs.detail = f.hints.detail;
    rs.maxSteps = f.hints.maxSteps;
    rs.maxDist = f.hints.maxDist;
    if (reset) {
        resetView();
        applyLook(f);
    }
    glfwSetWindowTitle(win, ("Fract3D - " + f.name).c_str());
}

// Restores the look-related defaults, then applies the fractal's curated "@look".
// With "keep my lighting" on, the lighting part of the current setup survives.
void App::applyLook(const Fractal& f) {
    const unsigned mask = kLook;
    RenderSettings defaults;
    // copy defaults for every look field, except lighting fields when they're kept
    visitRender(rs, [&](const char*, const char*, auto* ptr, int n, unsigned flags) {
        if (!(flags & mask) || (keepLighting && (flags & kLighting))) return;
        size_t off = reinterpret_cast<const char*>(ptr) - reinterpret_cast<const char*>(&rs);
        std::memcpy(ptr, reinterpret_cast<const char*>(&defaults) + off, sizeof(*ptr) * n);
    });
    int pal = 0;
    for (auto& [k, v] : f.look) {
        if (k == "palette") {
            for (int i = 0; i < (int)palettes.size(); i++)
                if (palettes[i].name == v) pal = i;
            continue;
        }
        bool known = false;
        visitRender(rs, [&](const char* name, const char* alias, auto* ptr, int n, unsigned flags) {
            if (!(flags & kLook) || (k != name && !(alias && k == alias))) return;
            known = true;
            if (keepLighting && (flags & kLighting)) return;
            std::string vals = v;
            std::replace(vals.begin(), vals.end(), ',', ' ');
            std::istringstream is(vals);
            for (int i = 0; i < n; i++) {
                double d;
                if (!(is >> d)) break;
                ptr[i] = static_cast<std::remove_reference_t<decltype(*ptr)>>(d);
            }
        });
        if (!known) fprintf(stderr, "fract3d: %s: unknown @look key '%s'\n", f.path.filename().c_str(), k.c_str());
    }
    rs.palette = pal;
    sanitize(rs);
    applyPalette();
}

void App::resetView() {
    Fractal& f = fractal();
    cam.lookAt(Vec3(f.camPos[0], f.camPos[1], f.camPos[2]), Vec3(f.camTarget[0], f.camTarget[1], f.camTarget[2]));
}

void App::setMode(ViewMode m) {
    mode = m;
    shownSig2D.clear();
    job2D.active = false;
    lastSig3D.clear();
}

// ------------------------------------------------------------------ frame
void App::frame() {
    glfwPollEvents();
    now = glfwGetTime();
    dt = (float)std::min(now - lastFrameTime, 0.1);
    lastFrameTime = now;
    fps = fps * 0.95f + (dt > 0 ? 1.0f / dt : 0) * 0.05f;
    glfwGetFramebufferSize(win, &fbW, &fbH);
    glfwGetWindowSize(win, &winW, &winH);
    if (fbW <= 0 || fbH <= 0) {  // minimized
        glfwWaitEventsTimeout(0.1);
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    handleKeys();
    if (mode == ViewMode::Fractal3D) input3D(dt);
    else input2D(dt);

    // live reload of shaders/ and fractals/ (edit a .glsl file and save)
    static double lastCheck = 0;
    if (now - lastCheck > 0.5) {
        lastCheck = now;
        if (rend.reloadCoreIfChanged()) {
            generation++;
            toast(rend.coreError.empty() ? "Shaders reloaded" : "Shader error - see Fractal tab", 3);
        }
        for (int i : lib_.reloadChanged()) {
            rend.dropPrograms(lib_.all()[i].key);
            generation++;
            toast("Reloaded " + lib_.all()[i].path.filename().string());
        }
    }

    if (!animPaused) animTime += dt;
    if (rs.cycleSpeed != 0.0f) cycleOffset = std::fmod(cycleOffset + rs.cycleSpeed * dt + 256.0f, 256.0f);

    rend.timer.poll();
    if (poster.active) updatePoster();
    else if (mode == ViewMode::Fractal3D) render3D();
    else render2D();
    if (mode == ViewMode::Fractal3D) updateProbe();

    // present
    GLuint target = 0;
    if (!cli.uiShotPath.empty()) {
        uiShotRT.ensure(fbW, fbH, GL_RGBA8, GL_NEAREST);
        target = uiShotRT.fbo;
    }
    rend.display(mode, &rend.accum, &rend.index2D, rs, cs, cycleOffset, fbW, fbH, target);

    drawUI();
    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, fbW, fbH);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!cli.uiShotPath.empty() && ++frameCount >= cli.uiShotFrames) {
        std::vector<uint8_t> px((size_t)fbW * fbH * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
        writePngRaw(cli.uiShotPath, fbW, fbH, px.data());
        printf("fract3d: ui-shot: samples=%d scale=%.2f perSampleFull=%.2fms deAtCam=%g centerHitT=%g camDist=%g\n", samples,
               (float)rend.accum.w / std::max(fbW, 1), perSampleMsFull, deAtCam, centerHitT, cam.distance);
        uiShotRT.release();
        quit = true;
    }
    glfwSwapBuffers(win);
}

// ------------------------------------------------------------------ keys
void App::handleKeys() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    bool ctrl = io.KeyCtrl;

    if (pressed(ImGuiKey_Tab)) showUI = !showUI;
    if (pressed(ImGuiKey_F1)) showHelp = !showHelp;
    if (pressed(ImGuiKey_F12)) takeScreenshot();
    if (pressed(ImGuiKey_L)) showLearn = !showLearn;
    if (pressed(ImGuiKey_M)) setMode(mode == ViewMode::Fractal3D ? ViewMode::Classic2D : ViewMode::Fractal3D);
    if (pressed(ImGuiKey_Escape) && showHelp) showHelp = false;
    if (ctrl && pressed(ImGuiKey_S)) savePar(userDir / "params" / (std::string(parName) + ".par"));
    if (ctrl && pressed(ImGuiKey_Q)) quit = true;
    if (pressed(ImGuiKey_N)) tourStep(io.KeyShift ? -1 : 1);
    if (pressed(ImGuiKey_C) && !ctrl) {  // Fractint's 'c': color cycling
        static float lastSpeed = 24.0f;
        if (rs.cycleSpeed != 0.0f) lastSpeed = rs.cycleSpeed, rs.cycleSpeed = 0.0f;
        else rs.cycleSpeed = lastSpeed;
        toast(rs.cycleSpeed != 0.0f ? "Color cycling on" : "Color cycling off", 1.2f);
    }
    if (pressed(ImGuiKey_F11)) {
        fullscreen = !fullscreen;
        if (fullscreen) {
            glfwGetWindowPos(win, &savedWin[0], &savedWin[1]);
            glfwGetWindowSize(win, &savedWin[2], &savedWin[3]);
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* vm = glfwGetVideoMode(mon);
            glfwSetWindowMonitor(win, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
        } else {
            glfwSetWindowMonitor(win, nullptr, savedWin[0], savedWin[1], savedWin[2], savedWin[3], 0);
        }
    }

    if (mode == ViewMode::Fractal3D) {
        if (pressed(ImGuiKey_P)) {
            rs.renderMode = 1 - rs.renderMode;
            toast(rs.renderMode ? "Path tracing: hold still to refine" : "Real-time rendering", 1.5f);
        }
        if (pressed(ImGuiKey_R) && !ctrl) resetView();
        if (pressed(ImGuiKey_Space)) animPaused = !animPaused;
        if (pressed(ImGuiKey_F) && centerHitT > 0) {
            rs.autoFocus = 0;
            rs.focusDist = centerHitT;
            toast("Focus set to the surface at screen center", 1.5f);
        }
        for (int k = 0; k < 9; k++)
            if (pressed((ImGuiKey)(ImGuiKey_1 + k)) && k < (int)lib_.all().size()) selectFractal(k, true);
    } else {
        if (pressed(ImGuiKey_O)) cs.showOrbit = !cs.showOrbit;
        if (pressed(ImGuiKey_B)) cs.banded = !cs.banded;
        if (pressed(ImGuiKey_Home)) {
            cs.julia = 0;
            cs.cx = cs.formula == 4 ? 0.0 : -0.6;
            cs.cy = 0;
            cs.height = 3.0;
        }
        if (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd)) cs.maxIter = std::min(cs.maxIter * 2, kMaxIterations);
        if (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract)) cs.maxIter = std::max(cs.maxIter / 2, 16);
    }
}

// ------------------------------------------------------------------ 3D input
void App::input3D(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    bool overUI = io.WantCaptureMouse;
    for (int b = 0; b < 3; b++)
        if (ImGui::IsMouseClicked(b) && !overUI && dragButton < 0) {
            dragButton = b;
            pressX = io.MousePos.x;
            pressY = io.MousePos.y;
        }
    if (dragButton >= 0 && !ImGui::IsMouseDown(dragButton)) dragButton = -1;
    if (ImGui::IsMouseDoubleClicked(0) && !overUI) {
        pickRequested = true;
        pickX = io.MousePos.x;
        pickY = io.MousePos.y;
    }

    float dx = io.MouseDelta.x, dy = io.MouseDelta.y;
    if (std::abs(dx) > 500 || std::abs(dy) > 500) dx = dy = 0;  // first frame after focus
    float tanHalf = std::tan(rs.fov * 0.5f * 0.0174533f);
    if (dragButton == 0 && !io.KeyShift) cam.orbit(dx * 0.006f, -dy * 0.006f);
    else if (dragButton == 1) cam.look(dx * 0.003f, -dy * 0.003f);
    else if (dragButton == 2 || (dragButton == 0 && io.KeyShift)) {
        float s = 2.0f * tanHalf / std::max(winH, 1);
        cam.pan(-dx * s, dy * s);
    }
    if (!overUI && io.MouseWheel != 0.0f) cam.dolly(std::pow(0.88f, io.MouseWheel));

    // WASD fly: speed follows the distance estimate, so you slow down near surfaces
    flying = false;
    if (!io.WantCaptureKeyboard && !io.KeyCtrl) {
        Vec3 d(0, 0, 0);
        if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) d.z += 1;
        if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) d.z -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) d.x += 1;
        if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) d.x -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_E)) d.y += 1;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) d.y -= 1;
        if (d.length() > 0) {
            flying = true;
            float base = deAtCam > 0 ? deAtCam : cam.distance * 0.01f;
            base = std::max(base, cam.distance * 1e-4f);
            float speed = base * flySpeed * (io.KeyShift ? 4.0f : 1.0f);
            cam.move(d.normalized(), speed * dt);
            // keep the orbit target on whatever surface is ahead, so orbiting feels right after flying
            if (centerHitT > 0) cam.setTargetDistance(cam.distance + (centerHitT - cam.distance) * std::min(dt * 4.0f, 1.0f));
        }
    }
}

// ------------------------------------------------------------------ 2D input
void App::input2D(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    bool overUI = io.WantCaptureMouse || !ImGui::IsMousePosValid();
    float sx = (float)fbW / std::max(winW, 1), sy = (float)fbH / std::max(winH, 1);
    double ps = cs.height / fbH;
    double mx = io.MousePos.x * sx, my = io.MousePos.y * sy;
    double px = cs.cx + (mx - fbW * 0.5) * ps, py = cs.cy + (fbH * 0.5 - my) * ps;
    if (!ImGui::IsMousePosValid()) px = cs.cx, py = cs.cy;  // keyboard zoom without a mouse: zoom at center

    for (int b = 0; b < 3; b++)
        if (ImGui::IsMouseClicked(b) && !overUI && dragButton < 0) {
            dragButton = b;
            pressX = io.MousePos.x;
            pressY = io.MousePos.y;
        }
    if (dragButton == 0 || dragButton == 2) {
        cs.cx -= io.MouseDelta.x * sx * ps;
        cs.cy += io.MouseDelta.y * sy * ps;
    }
    auto toggleJulia = [&]() {
        if (cs.formula == 4) {
            toast("Newton's method has no Julia/Mandelbrot pair", 2);
            return;
        }
        if (!cs.julia) {
            savedMandel[0] = cs.cx;
            savedMandel[1] = cs.cy;
            savedMandel[2] = cs.height;
            cs.jx = px;
            cs.jy = cs.formula == 1 ? -py : py;
            cs.julia = 1;
            cs.cx = 0;
            cs.cy = 0;
            cs.height = 3.2;
            char buf[128];
            snprintf(buf, sizeof buf, "Julia set for c = %.6f %+.6fi", cs.jx, cs.jy);
            toast(buf);
        } else {
            cs.julia = 0;
            cs.cx = savedMandel[0];
            cs.cy = savedMandel[1];
            cs.height = savedMandel[2];
            toast("Back to the parameter plane");
        }
    };
    if (dragButton == 1 && ImGui::IsMouseReleased(1)) {
        if (std::abs(io.MousePos.x - pressX) + std::abs(io.MousePos.y - pressY) < 5) toggleJulia();
    }
    if (dragButton >= 0 && !ImGui::IsMouseDown(dragButton)) dragButton = -1;
    if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Space, false) && !overUI) toggleJulia();

    float wheel = overUI ? 0.0f : io.MouseWheel;
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyDown(ImGuiKey_PageUp)) wheel += dt * 4;
        if (ImGui::IsKeyDown(ImGuiKey_PageDown)) wheel -= dt * 4;
    }
    if (wheel != 0.0f) {
        double f = std::pow(0.8, wheel);
        cs.cx = px + (cs.cx - px) * f;
        cs.cy = py + (cs.cy - py) * f;
        cs.height = std::clamp(cs.height * f, 1e-15, 50.0);
    }
}

// ------------------------------------------------------------------ 3D rendering
View3D App::makeView(int w, int h) const {
    View3D v;
    v.pos = cam.pos;
    v.fwd = cam.forward();
    v.right = cam.right();
    v.up = cam.up();
    v.tanHalfFov = std::tan(rs.fov * 0.5f * 0.0174533f);
    v.sceneScale = cam.distance;
    v.focusDist = rs.autoFocus ? (centerHitT > 0 ? centerHitT : cam.distance) : rs.focusDist;
    v.time = (float)now;
    v.animTime = animTime;
    v.fullW = w;
    v.fullH = h;
    return v;
}

template <class T>
static void appendBytes(std::vector<uint8_t>& v, const T& x) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&x);
    v.insert(v.end(), p, p + sizeof(T));
}

std::vector<uint8_t> App::signature3D(int w, int h) const {
    RenderSettings r = rs;
    // palette cycling only matters when the palette is used for coloring
    if (r.colorMode <= 2) r.colorOffset = rs.colorOffset + cycleOffset / 256.0f;
    // the focus only matters with a lens
    if (r.aperture <= 0.0f) r.focusDist = 0, r.autoFocus = false;
    else if (r.autoFocus) r.focusDist = 0;  // autofocus distance is added (quantized) below
    std::vector<uint8_t> s;
    s.reserve(512);
    appendFields(s, r, kTrace3D, [](RenderSettings& x, auto&& f) { visitRender(x, f); });
    appendBytes(s, rs.palette);
    appendBytes(s, cam.pos);
    appendBytes(s, cam.yaw);
    appendBytes(s, cam.pitch);
    appendBytes(s, cam.distance);
    appendBytes(s, current_);
    appendBytes(s, generation);
    appendBytes(s, paletteVersion);
    appendBytes(s, w);
    appendBytes(s, h);
    if (rs.aperture > 0.0f && rs.autoFocus) {
        // quantize so probe noise doesn't restart accumulation every frame
        float fd = makeView(w, h).focusDist;
        int q = (int)std::lround(std::log(std::max(fd, 1e-9f)) * 200.0f);
        appendBytes(s, q);
    }
    for (auto& p : lib_.all()[current_].params) {
        float v[4];
        paramEffective(p, animTime, v);
        appendBytes(s, v);
    }
    return s;
}

void App::render3D() {
    Fractal& f = fractal();
    auto sig = signature3D(fbW, fbH);
    if (sig != lastSig3D) {
        lastSig3D = std::move(sig);
        samples = 0;
        band3DRow = 0;
        lastChange = now;
    }
    interactive = now - lastChange < 0.25;

    // adaptive resolution: estimate cost per full-res sample from GPU timer results
    if (rend.timer.fresh && rend.timer.lastTag[0] > 0) {
        float scale = rend.timer.lastTag[0], n = rend.timer.lastTag[1];
        float est = (float)rend.timer.lastMs / std::max(n, 1.0f) / (scale * scale);
        perSampleMsFull = perSampleMsFull * 0.7f + est * 0.3f;
    }
    float targetMs = 1000.0f / std::max(rs.targetFps, 10.0f) * 0.8f;
    if (rs.adaptiveRes) motionScale = std::clamp(std::sqrt(targetMs / std::max(perSampleMsFull, 0.01f)), 0.2f, rs.stillScale);
    else motionScale = rs.stillScale;

    float scale = interactive ? motionScale : rs.stillScale;
    // snap to steps so small estimate changes don't reallocate the buffer
    scale = std::clamp(std::round(scale * 20.0f) / 20.0f, 0.1f, 2.0f);
    int rw = std::max(1, (int)(fbW * scale)), rh = std::max(1, (int)(fbH * scale));
    if (rend.accum.w != rw || rend.accum.h != rh) {
        rend.accum.ensure(rw, rh, GL_RGBA32F, GL_LINEAR);
        samples = 0;
        band3DRow = 0;
    }
    int maxS = rs.renderMode ? rs.maxSamplesPT : rs.maxSamplesRT;
    if (samples >= maxS) return;  // converged: GPU idles
    if (samples == 0 && band3DRow == 0) rend.clear3D(rend.accum);

    View3D v = makeView(rw, rh);
    float perSample = perSampleMsFull * scale * scale;
    if (!interactive && perSample > targetMs * 2.0f) {
        // A full sample would stall the desktop: render it in bands over several frames.
        // Each pixel's alpha counts its own samples, so a partly finished pass is fine.
        int rows = std::clamp((int)(targetMs / (perSample / rh)), 1, rh - band3DRow);
        int sc[4] = {0, rh - band3DRow - rows, rw, rows};
        rend.timer.begin(scale, (float)rows / rh);
        bool ok = rend.renderSample3D(rend.accum, samples, f, rs, v, sc);
        rend.timer.end();
        if (!ok) return;
        band3DRow += rows;
        if (band3DRow >= rh) {
            band3DRow = 0;
            samples++;
        }
        return;
    }
    band3DRow = 0;
    int n = 1;
    if (!interactive) {
        n = std::clamp((int)(targetMs / std::max(perSample, 0.01f)), 1, 16);
        n = std::min(n, maxS - samples);
    }
    rend.timer.begin(scale, (float)n);
    for (int i = 0; i < n; i++)
        if (rend.renderSample3D(rend.accum, samples, f, rs, v)) samples++;
    rend.timer.end();
}

void App::updateProbe() {
    float de, hit;
    while (!probeTags.empty() && rend.fetchProbe(de, hit)) {
        int tag = probeTags.front();
        Vec3 dir = probeDirs.front();
        probeTags.erase(probeTags.begin());
        probeDirs.erase(probeDirs.begin());
        if (tag == 0) {
            deAtCam = de;
            centerHitT = hit;
        } else if (hit > 0) {
            cam.lookAt(cam.pos, cam.pos + dir * hit);
            toast("Orbit center moved to the point you double-clicked", 1.5f);
        } else {
            toast("Nothing there - double-click on the fractal surface", 1.5f);
        }
    }
    View3D v = makeView(fbW, fbH);
    float pixelAngle = 2.0f * v.tanHalfFov / std::max(fbH, 1);
    Vec3 dir = v.fwd;
    int tag = 0;
    if (pickRequested) {
        float sx = (float)fbW / std::max(winW, 1), sy = (float)fbH / std::max(winH, 1);
        float ux = (float)(pickX * sx - fbW * 0.5) / fbH, uy = (float)(fbH * 0.5 - pickY * sy) / fbH;
        dir = (v.fwd + (v.right * ux + v.up * uy) * (2.0f * v.tanHalfFov)).normalized();
        tag = 1;
    }
    if (rend.probe(fractal(), rs, v, dir, pixelAngle)) {
        probeTags.push_back(tag);
        probeDirs.push_back(dir);
        if (tag == 1) pickRequested = false;
    }
}

// ------------------------------------------------------------------ 2D rendering
bool App::stepJob2D(Job2D& job, IndexTarget& target, double budgetMs) {
    int tw = target.w, th = target.h;
    int maxIter = std::max(job.cs.maxIter, 1);
    glFinish();  // drain earlier work so pass timings measure only ours
    auto t0 = std::chrono::steady_clock::now();
    while (job.active) {
        bool first = job.bandRows == 0;
        if (first) {
            job.bandRows = std::min(rend.bandRowsFor(tw), th - job.row);
            job.itersDone = 0;
        }
        int k = std::min(job.chunk, maxIter - job.itersDone);
        int y0 = th - job.row - job.bandRows;  // bands run top-down (GL rows count up)
        auto b0 = std::chrono::steady_clock::now();
        if (!rend.dispatch2D(target, job.cs, y0, job.bandRows, std::max(k, 1), first)) {
            job.active = false;
            return false;
        }
        glFinish();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - b0).count();
        job.itersDone += std::max(k, 1);
        // aim each pass at a third of the budget; pixels that finish make later passes cheaper
        double f = std::clamp(budgetMs / 3.0 / std::max(ms, 0.01), 0.25, 4.0);
        job.chunk = (int)std::clamp(job.chunk * f, 16.0, (double)maxIter);
        if (job.itersDone >= maxIter) {  // every orbit in the band has escaped or hit the limit
            job.row += job.bandRows;
            job.bandRows = 0;
            if (job.row >= th) {
                job.active = false;
                return true;
            }
        }
        double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (total > budgetMs) break;
    }
    return false;
}

void App::render2D() {
    if (cs.formula == kCustomFormula && !rend.hasCustomFormula()) {
        cs.formula = 0;
        toast("No custom formula is compiled - showing the Mandelbrot set instead", 4);
    }
    // only fields that change the iteration buffer need a recompute
    Classic2DSettings c = cs;
    std::vector<uint8_t> sig;
    appendFields(sig, c, kCompute2D, [](Classic2DSettings& x, auto&& f) { visitClassic(x, f); });
    appendBytes(sig, formulaGeneration);
    appendBytes(sig, fbW);
    appendBytes(sig, fbH);
    appendBytes(sig, generation);
    if (sig != coreSig2D) {
        coreSig2D = sig;
        lastChange = now;
    }
    interactive = now - lastChange < 0.2;
    int ss = interactive ? 1 : std::clamp(cs.supersample, 1, 4);
    appendBytes(sig, ss);
    int tw = fbW * ss, th = fbH * ss;

    // (re)start a job when the wanted image differs from what's shown or being rendered
    bool wantNew = job2D.active ? job2D.sig != sig : shownSig2D != sig;
    if (wantNew) {
        job2D.active = true;
        job2D.row = 0;
        job2D.bandRows = 0;
        job2D.sig = sig;
        job2D.cs = cs;
        job2D.cs.supersample = ss;
        job2D.chunk = std::min(std::max(cs.maxIter, 1), 512);
        // If index2D holds a complete image for this window (at any supersampling), keep
        // showing it: draw over it when the size matches (the reveal), otherwise render
        // offscreen and swap when done. After a resize there's nothing valid to keep.
        bool shownValid = rend.index2D.w > 0 && rend.index2D.w % fbW == 0 && rend.index2D.h == fbH * (rend.index2D.w / fbW);
        job2D.offscreen = shownValid && (rend.index2D.w != tw || rend.index2D.h != th);
        IndexTarget& t = job2D.offscreen ? work2D : rend.index2D;
        if (t.w != tw || t.h != th) {
            if (!t.ensure(tw, th)) {
                job2D.active = false;
                toast("Not enough video memory for this image size");
                return;
            }
            t.clear();
        }
    }
    if (!job2D.active) return;
    IndexTarget& target = job2D.offscreen ? work2D : rend.index2D;
    if (stepJob2D(job2D, target, frameBudgetMs)) {
        if (job2D.offscreen) {
            rend.index2D.swap(work2D);
            work2D.release();
        }
        shownSig2D = job2D.sig;
    }
}

// ------------------------------------------------------------------ screenshots & posters
void App::takeScreenshot() {
    std::error_code ec;
    fs::create_directories(picturesDir, ec);
    std::string base = (picturesDir / ("fract3d-" + timestampName())).string();
    std::vector<uint8_t> px;
    bool ok = rend.readImage(mode, &rend.accum, &rend.index2D, rs, cs, cycleOffset, fbW, fbH, px) &&
              writePngRaw(base + ".png", fbW, fbH, px.data());
    savePar(base + ".par");
    toast(ok ? "Saved " + base + ".png (+ .par to recreate it)" : "Screenshot failed", 4);
}

void App::startPoster(int w, int h, int nSamples) {
    poster.active = true;
    poster.w = w;
    poster.h = h;
    poster.samples = mode == ViewMode::Fractal3D ? std::max(nSamples, 1) : 1;
    poster.done = 0;
    poster.tile = 0;
    poster.started = glfwGetTime();
    poster.view = makeView(w, h);
    poster.rs = rs;
    poster.cs = cs;
    poster.cs.supersample = std::clamp(cs.supersample, 1, 4);
    poster.fractal = fractal();
    for (auto& p : poster.fractal.params) {  // freeze animated parameters at their current value
        paramEffective(p, animTime, p.value);
        p.animate = false;
    }
    poster.palette = palettes[rs.palette];
    poster.cycleOffset = cycleOffset;
    poster.par = parText();
    poster.mode = mode;
    std::error_code ec;
    fs::create_directories(picturesDir, ec);
    poster.path = (picturesDir / ("fract3d-" + timestampName() + "-" + std::to_string(w) + "x" + std::to_string(h) + ".png")).string();
    bool ok;
    if (mode == ViewMode::Fractal3D) {
        ok = poster.target.ensure(w, h, GL_RGBA32F, GL_LINEAR);
        if (ok) rend.clear3D(poster.target);
    } else {
        if (poster.cs.formula == kCustomFormula && !rend.hasCustomFormula()) poster.cs.formula = cs.formula = 0;
        int ss = poster.cs.supersample;
        ok = poster.index.ensure(w * ss, h * ss);
        if (ok) {
            poster.index.clear();
            poster.job = Job2D();
            poster.job.active = true;
            poster.job.cs = poster.cs;
            poster.job.chunk = std::min(std::max(cs.maxIter, 1), 512);
        }
    }
    if (!ok) {
        char buf[160];
        snprintf(buf, sizeof buf, "Can't render %dx%d: too large for this GPU (max %d per side, including anti-aliasing)",
                 w, h, maxTextureSize());
        toast(buf, 6);
        poster.active = false;
        exitCode = 1;
        if (!cli.shotPath.empty()) quit = true;
    }
}

void App::updatePoster() {
    const int T = 1024;
    bool is3D = poster.mode == ViewMode::Fractal3D;
    double budget = cli.shotPath.empty() ? 30.0 : 500.0;
    bool finished = false;
    if (is3D) {
        if (rend.status(poster.fractal) == Renderer::ProgStatus::Compiling) return;  // wait for the shaders
        int tw = poster.target.w, th = poster.target.h;
        int tilesX = (tw + T - 1) / T, tilesY = (th + T - 1) / T, tiles = tilesX * tilesY;
        const View3D& v = poster.view;
        // Tiles + glFinish keep each GPU submission short, so the desktop stays responsive.
        auto t0 = std::chrono::steady_clock::now();
        while (poster.done < poster.samples) {
            int tx = poster.tile % tilesX, ty = poster.tile / tilesX;
            int sc[4] = {tx * T, ty * T, std::min(T, tw - tx * T), std::min(T, th - ty * T)};
            if (!rend.renderSample3D(poster.target, poster.done, poster.fractal, poster.rs, v, sc)) {
                toast("Render failed (shader error)");
                poster.active = false;
                poster.target.release();
                exitCode = 1;
                if (!cli.shotPath.empty()) quit = true;
                return;
            }
            if (++poster.tile >= tiles) {
                poster.tile = 0;
                poster.done++;
            }
            glFinish();
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (ms > budget) break;
        }
        finished = poster.done >= poster.samples;
    } else {
        finished = stepJob2D(poster.job, poster.index, budget);
        poster.done = finished ? 1 : 0;
        if (!finished && !poster.job.active) {  // dispatch failed
            toast("Render failed (shader error)");
            poster.active = false;
            poster.index.release();
            exitCode = 1;
            if (!cli.shotPath.empty()) quit = true;
            return;
        }
    }
    if (finished) {
        std::vector<uint8_t> px;
        rend.setPalette(poster.palette);
        bool ok = rend.readImage(poster.mode, &poster.target, &poster.index, poster.rs, poster.cs, poster.cycleOffset,
                                 poster.w, poster.h, px) &&
                  writePngRaw(poster.path, poster.w, poster.h, px.data());
        applyPalette();  // back to whatever the live view uses
        std::ofstream(fs::path(poster.path).replace_extension(".par")) << poster.par;
        double secs = glfwGetTime() - poster.started;
        char buf[512];
        snprintf(buf, sizeof buf, "%s %s (%.1fs)", ok ? "Saved" : "FAILED to save", poster.path.c_str(), secs);
        toast(buf, 6);
        if (!ok) exitCode = 1;
        poster.active = false;
        poster.target.release();
        poster.index.release();
        lastSig3D.clear();
        shownSig2D.clear();
        if (!cli.shotPath.empty()) quit = true;
    }
}

// ------------------------------------------------------------------ 2D <-> 3D bridge
void App::liftTo3D() {
    int li = lib_.indexOf("landscape");
    if (li < 0) {
        toast("landscape.glsl not found");
        return;
    }
    Fractal& f = lib_.all()[li];
    int formula = 0;
    switch (cs.formula) {
    case 1: formula = 1; break;
    case 2: formula = 2; break;
    case 3: formula = 3; break;
    default: formula = 0;
    }
    if (cs.julia && cs.formula == 0) formula = 4;
    if (auto* p = f.find("formula")) p->value[0] = (float)formula;
    if (auto* p = f.find("center")) { p->value[0] = (float)cs.cx; p->value[1] = (float)cs.cy; }
    if (auto* p = f.find("zoom")) p->value[0] = (float)(3.0 / cs.height);
    if (auto* p = f.find("iterations")) p->value[0] = (float)std::clamp(cs.maxIter, 10, 1500);
    if (auto* p = f.find("juliaC")) { p->value[0] = (float)cs.jx; p->value[1] = (float)cs.jy; }
    selectFractal(li, true);
    setMode(ViewMode::Fractal3D);
    rs.colorMode = 0;
    rs.colorScale = cs.colorDensity;
    rs.colorOffset = 0;
    rs.paletteMix = 1.0f;
    if (cs.height < 1e-4) toast("Note: 3D uses single precision; very deep zooms get blocky", 4);
    else toast("Lifted into 3D: height = escape time");
}

void App::flattenTo2D() {
    Fractal& f = fractal();
    if (f.key == "landscape") {
        int formula = (int)f.find("formula")->value[0];
        cs.formula = formula == 4 ? 0 : formula;
        cs.julia = formula == 4;
        if (formula == 3) cs.power = 3;
        cs.cx = f.find("center")->value[0];
        cs.cy = f.find("center")->value[1];
        cs.height = 3.0 / f.find("zoom")->value[0];
        cs.jx = f.find("juliaC")->value[0];
        cs.jy = f.find("juliaC")->value[1];
        cs.maxIter = (int)f.find("iterations")->value[0];
    }
    setMode(ViewMode::Classic2D);
}
