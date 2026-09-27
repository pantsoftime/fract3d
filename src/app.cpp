#include "app.h"
#include "pngmeta.h"
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
#include <thread>

#include <unistd.h>

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
    session.cli = opts;
    session.dataDir = findDataDir();
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    session.userDir = (xdg && *xdg ? fs::path(xdg) : homeDir() / ".config") / "fract3d";
    session.picturesDir = homeDir() / "Pictures" / "fract3d";
    std::error_code ec;
    fs::create_directories(session.userDir / "params", ec);
    fs::create_directories(session.userDir / "palettes", ec);

    glfwSetErrorCallback([](int code, const char* msg) { fprintf(stderr, "glfw error %d: %s\n", code, msg); });
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#ifndef NDEBUG
    session.cli.glDebug = true;  // debug builds always report GL errors
#endif
    if (session.cli.glDebug) glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    if (session.cli.hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    // --ui-shot captures the window, so it gets the requested size; --render draws
    // offscreen at any size, and its (hidden) window only needs to exist
    bool uiShot = (!session.cli.uiShotPath.empty() || !session.cli.uiRecord.empty()) && session.cli.shotW;
    int ww = uiShot ? session.cli.shotW : 1600, wh = uiShot ? session.cli.shotH : 900;
    win = glfwCreateWindow(ww, wh, "Fract3D", nullptr, nullptr);
    if (!win) {
        fprintf(stderr, "fract3d: could not create an OpenGL 4.6 window\n");
        return false;
    }
    glfwMakeContextCurrent(win);
    glfwSetWindowUserPointer(win, this);
    glfwSetDropCallback(win, [](GLFWwindow* w, int n, const char** paths) {
        auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
        for (int i = 0; i < n; i++) app->droppedFiles.push_back(paths[i]);
    });
    glfwSwapInterval(1);
    glfwGetFramebufferSize(win, &fbW, &fbH);
    glfwGetWindowSize(win, &winW, &winH);

    printf("fract3d: %s | %s\n", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    printf("fract3d: data from %s\n", session.dataDir.c_str());
    if (session.cli.glDebug) installGlDebugOutput(session.cli.glDebugVerbose);

    std::string err;
    if (!rend.init(session.dataDir, err)) {
        fprintf(stderr, "fract3d: shader setup failed:\n%s\n", err.c_str());
        return false;
    }
    lib_.load(session.dataDir / "fractals");
    if (lib_.all().empty()) {
        fprintf(stderr, "fract3d: no fractals found in %s/fractals\n", session.dataDir.c_str());
        return false;
    }
    rend.warmUp(lib_.all());  // start compiling every fractal in the background

    palettes = builtinPalettes();
    for (auto dir : {session.dataDir / "palettes", session.userDir / "palettes"}) {
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
    palettes.push_back(makeCosinePalette("Custom (cosine editor)", view.cosine));
    gradientPaletteIdx = (int)palettes.size();
    palettes.push_back(makeGradientPalette("Custom (gradient editor)", view.gradient));

    refreshDocs();
    for (auto& f : session.cli.frmFiles) {
        bool ok = false;
        auto defs = parseFormulaFile(readTextFile(f, &ok));
        if (!ok) fprintf(stderr, "fract3d: can't read %s\n", f.c_str());
        formulas.insert(formulas.end(), defs.begin(), defs.end());
    }
    // a user formula is always compiled, so "Custom formula" works from the start
    if (!selectFormula(session.cli.formula.empty() ? "Mandel" : session.cli.formula) && !formulas.empty()) {
        if (!session.cli.formula.empty()) fprintf(stderr, "fract3d: no formula named %s\n", session.cli.formula.c_str());
        selectFormula(formulas.front().name);
    }

    loadPrefs();
    if (session.cli.theme >= 0) session.uiTheme = session.cli.theme;
    setupImGui();

    int idx = session.cli.fractal.empty() ? 0 : std::max(lib_.indexOf(session.cli.fractal), 0);
    selectFractal(idx, true);
    const auto& c = session.cli;
    bool askedForSomething = !c.parFile.empty() || !c.fractal.empty() || !c.formula.empty() || c.mode2d || c.hidden;
    if (session.restoreSession && !askedForSomething) {
        bool ok = false;
        std::string last = readTextFile((session.userDir / "session.par").string(), &ok);
        if (ok && loadParText(last, "last session", true)) toast("Welcome back - restored your last view", 3);
    }
    recordHistoryNow();  // the starting view is the first history entry
    if (!session.cli.parFile.empty() && !loadPar(session.cli.parFile)) {
        fprintf(stderr, "fract3d: could not load %s\n", session.cli.parFile.c_str());
        if (!session.cli.shotPath.empty()) {  // an offline render of the wrong view would be worse than none
            exitCode = 1;
            quit = true;
            return true;
        }
    }
    if (session.cli.mode2d) view.mode = ViewMode::Classic2D;
    if (!session.cli.formula.empty()) {
        view.mode = ViewMode::Classic2D;
        view.cs.formula = kCustomFormula;
    }
    if (session.cli.pathTrace >= 0) view.rs.renderMode = session.cli.pathTrace;
    ui.showJuliaInset = session.cli.insetOn;
    wheelPending = session.cli.wheel;
    if (session.cli.hideUi) ui.showUI = false;
    cockpit.show = session.cli.cockpit;
    ui.showPanels = !session.cli.hidePanels;
    if (session.cli.autopilotSpeed > 0) autopilot.speedFactor = std::clamp(session.cli.autopilotSpeed, 0.2f, 5.0f);
    if (recording()) view.rs.adaptiveRes = false;  // (every recorded frame at full resolution)
    for (auto& w : session.cli.openWindows) {
        if (w == "gradient") {
            view.gradient = sampleStops(palettes[view.rs.palette], 8);
            view.rs.palette = gradientPaletteIdx;
            applyPalette();
            ui.showGradientEditor = true;
        } else if (w == "formula") {
            ui.formulaEdit = view.formulaSource;
            ui.showFormulaEditor = true;
        } else if (w == "help") {
            ui.showHelp = true;
        } else if (w == "render") {
            ui.showPoster = true;
        } else if (w == "path") {
            ui.showPathWindow = true;
        }
    }
    if (session.cli.orbitOn) view.cs.showOrbit = true;
    sanitize(view.rs);
    sanitize(view.cs);
    applyPalette();

    if (session.cli.selfTest) {
        exitCode = selfTest();
        quit = true;
        return true;
    }
    if (!session.cli.pathFile.empty() && !loadPath(session.cli.pathFile)) {
        fprintf(stderr, "fract3d: could not load camera path %s\n", session.cli.pathFile.c_str());
        exitCode = 1;
        quit = true;
        return true;
    }
    auto isVideo = [](const std::string& p) {
        if (isImageSequence(p)) return true;  // frame-%05d.png
        for (const char* e : {".mp4", ".mkv", ".mov", ".webm"})
            if (p.size() > 4 && p.compare(p.size() - strlen(e), strlen(e), e) == 0) return true;
        return false;
    };
    if (!session.cli.shotPath.empty() && isVideo(session.cli.shotPath)) {
        if (session.cli.shotW) camPath.videoW = session.cli.shotW & ~1, camPath.videoH = session.cli.shotH & ~1;  // yuv420p
        if (session.cli.shotW && (camPath.videoW != session.cli.shotW || camPath.videoH != session.cli.shotH))
            printf("fract3d: video sizes must be even: using %dx%d\n", camPath.videoW, camPath.videoH);
        if (session.cli.shotSamples) camPath.videoSamples = session.cli.shotSamples;
        camPath.fps = std::clamp(session.cli.videoFps, 1.0f, 240.0f);
        if (!startVideo(session.cli.shotPath)) {
            exitCode = 1;
            quit = true;
        }
        lastFrameTime = glfwGetTime();
        return true;
    }
    if (session.cli.pathTime >= 0 && !camPath.keys.empty()) {
        applyPathTime(session.cli.pathTime);  // one frame of the path (tests, stills)
        endPathPreview();
    }
    if (!session.cli.shotPath.empty()) {
        int w = session.cli.shotW ? session.cli.shotW : fbW, h = session.cli.shotH ? session.cli.shotH : fbH;
        int s = session.cli.shotSamples ? session.cli.shotSamples : (view.rs.renderMode ? 256 : 32);
        startPoster(w, h, s);
        poster.path = session.cli.shotPath;
    }
    lastFrameTime = glfwGetTime();
    return true;
}

void App::shutdown() {
    if (!session.cli.hidden) savePrefs();
    saveSession(true);
    if (video.active) cancelVideo();  // closes the encoder pipe cleanly
    pollVideoEncoder(true);           // a finished export may still be flushing: let it complete
    // every GL object must go before the context does
    poster.target.release();
    poster.index.release();
    work2D.release();
    uiShotRT.release();
    inset.index.release();
    inset.image.release();
    cockpit.map.release();
    rend.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (win) glfwDestroyWindow(win);
    glfwTerminate();
}

int App::run() {
    while (!glfwWindowShouldClose(win) && !quit) frame();
    if (session.cli.flightReport)
        printf("fract3d: flight: %s %.0f s, closest %.3g of the distance kept, %.1f of those flown, %d frames touching\n", fractal().key.c_str(),
               flightStats.seconds, flightStats.closest, flightStats.travelled, flightStats.touches);
    return 0;
}

// ------------------------------------------------------------------ Learn-panel docs
// Docs and presets are data files like the shaders: edits show up without a restart.
void App::refreshDocs() {
    std::error_code ec;
    fs::path docs = session.dataDir / "docs", presets = session.dataDir / "presets";
    // formula files: built-in ones, then the user's own
    {
        fs::file_time_type ft = fs::file_time_type::min();  // NB: file_time_type{} is libstdc++'s epoch in 2174
        std::vector<fs::path> files;
        for (auto dir : {session.dataDir / "formulas", session.userDir / "formulas"}) {
            ft = std::max(ft, fs::last_write_time(dir, ec));
            std::vector<fs::path> here;
            for (auto& e : fs::directory_iterator(dir, ec))
                if (e.path().extension() == ".frm") here.push_back(e.path()), ft = std::max(ft, fs::last_write_time(e.path(), ec));
            std::sort(here.begin(), here.end());
            files.insert(files.end(), here.begin(), here.end());
        }
        if (ft != ui.formulasTime) {
            ui.formulasTime = ft;
            formulas.clear();
            for (auto& f : files)
                for (auto& d : parseFormulaFile(readTextFile(f.string()))) formulas.push_back(d);
        }
    }
    auto newest = [&](const fs::path& dir) {
        fs::file_time_type t = fs::last_write_time(dir, ec);  // changes when files are added/removed
        for (auto& e : fs::directory_iterator(dir, ec)) t = std::max(t, fs::last_write_time(e.path(), ec));
        return t;
    };
    fs::file_time_type dt = newest(docs), pt = newest(presets);
    if (dt != ui.docsTime) {
        ui.docsTime = dt;
        ui.conceptsText = readTextFile((docs / "concepts.txt").string());
        ui.classicText = readTextFile((docs / "classic.txt").string());
    }
    if (pt != ui.presetsTime) {
        ui.presetsTime = pt;
        std::vector<fs::path> files;
        for (auto& e : fs::directory_iterator(presets, ec))
            if (e.path().extension() == ".par") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        ui.tourStops.clear();
        for (auto& f : files) {
            std::string first;
            std::istringstream is(readTextFile(f.string()));
            std::getline(is, first);
            size_t s = first.find_first_not_of("; ");
            ui.tourStops.push_back({f, first.rfind(";", 0) == 0 && s != std::string::npos ? first.substr(s) : ""});
        }
    }
}

// ------------------------------------------------------------------ user formulas
const FormulaDef* App::findFormula(const std::string& name) const {
    std::string want = name;
    std::transform(want.begin(), want.end(), want.begin(), ::tolower);
    for (auto& f : formulas) {
        std::string n = f.name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        if (n == want) return &f;
    }
    return nullptr;
}

bool App::selectFormula(const std::string& name) {
    const FormulaDef* f = findFormula(name);
    if (!f) return false;
    view.formulaName = f->name;
    view.formulaSource = f->source;
    ui.formulaEdit = f->source;
    // the formula's suggested parameters and view (see the @ lines in formulas/*.frm)
    for (int i = 0; i < kFormulaParams; i++) {
        view.cs.formulaP[i][0] = f->hasP[i] ? f->p[i][0] : 0.0f;
        view.cs.formulaP[i][1] = f->hasP[i] ? f->p[i][1] : 0.0f;
    }
    for (int i = 0; i < 4; i++)
        if (f->fn[i] >= 0) view.fn[i] = f->fn[i];
    if (f->hasView) {
        view.cs.cx = f->view[0];
        view.cs.cy = f->view[1];
        view.cs.height = f->view[2];
    }
    return compileFormula();
}

bool App::compileFormula() {
    // Keyframes, undo and PAR loads ask for the same formula again and again; the GPU
    // compile (and the landscape rebuild it causes) only happens when something changed.
    if (view.formulaSource == compiledSource && std::equal(view.fn, view.fn + 4, compiledFn) && rend.hasCustomFormula()) {
        formulaError.clear();
        return true;
    }
    TranspiledFormula t = transpileFormula(view.formulaSource, view.fn);
    if (!t.ok) {
        formulaError = t.error;
        fprintf(stderr, "fract3d: formula %s: %s\n", view.formulaName.c_str(), formulaError.c_str());
        return false;
    }
    rend.setCustomFormula(t.glsl);
    if (!rend.customFormulaError().empty()) {  // shouldn't happen: the transpiler emits valid GLSL
        formulaError = "GLSL: " + rend.customFormulaError();
        fprintf(stderr, "fract3d: formula %s: %s\n", view.formulaName.c_str(), formulaError.c_str());
        return false;
    }
    formulaError.clear();
    formulaInfo = t;
    formulaGeneration++;
    compiledSource = view.formulaSource;
    std::copy(view.fn, view.fn + 4, compiledFn);
    // A render in progress that uses the formula would now mix the old and new
    // formula (the GPU programs were just rebuilt): start it over.
    if (poster.active && !poster.toVideo) {
        const Param* lf = poster.fractal.find("formula");
        bool uses = poster.mode == ViewMode::Classic2D ? poster.cs.formula == kCustomFormula
                                                        : poster.fractal.key == "landscape" && lf && std::lround(lf->value[0]) == 5;
        if (uses) {
            std::string path = poster.path;
            startPoster(poster.w, poster.h, poster.samples);
            poster.path = path;
            toast("The formula changed - the high-res render started over", 4);
        }
    }
    return true;
}

// ------------------------------------------------------------------ drag and drop
// Drop a screenshot (PNG with an embedded view), a .par, a .frm formula file or
// a .map palette onto the window.
void App::openDroppedFile(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".png" || ext == ".par") {
        if (!loadPar(p)) toast(ext == ".png" ? p.filename().string() + " doesn't contain a Fract3D view" : "Couldn't read " + p.string(), 4);
    } else if (ext == ".frm") {
        auto defs = parseFormulaFile(readTextFile(p.string()));
        if (defs.empty()) return toast("No formulas found in " + p.filename().string(), 4);
        formulas.insert(formulas.end(), defs.begin(), defs.end());
        selectFormula(defs.front().name);
        view.cs.formula = kCustomFormula;
        setMode(ViewMode::Classic2D);
        toast("Loaded " + std::to_string(defs.size()) + " formula(s) from " + p.filename().string() +
                  " (copy it to ~/.config/fract3d/formulas to keep them)", 5);
    } else if (ext == ".map") {
        Palette pal;
        if (!loadMapFile(p.string(), pal)) return toast("Couldn't read palette " + p.filename().string(), 4);
        pal.name = "MAP: " + pal.name;
        palettes.insert(palettes.begin() + customPaletteIdx, pal);  // keep the two editable ones last
        view.rs.palette = customPaletteIdx++;
        gradientPaletteIdx++;
        applyPalette();
        toast("Palette " + pal.name + " loaded", 3);
    } else {
        toast("Drop a PNG screenshot, a .par (Fract3D's or Fractint's), .frm or .map file", 3);
    }
}

// ------------------------------------------------------------------ undo / history
std::string App::historyLabel() const {
    char t[16];
    std::time_t now_ = std::time(nullptr);
    std::strftime(t, sizeof t, "%H:%M:%S", std::localtime(&now_));
    std::string what = view.mode == ViewMode::Classic2D
                           ? std::string(kClassicFormulas[view.cs.formula]) + (view.cs.julia ? " Julia" : "") +
                                 (view.cs.formula == kCustomFormula ? " (" + view.formulaName + ")" : "")
                           : lib_.all()[view.fractal].name;
    return std::string(t) + "  " + what;
}

// A new entry is recorded once the view has stayed the same for 0.6 s after a
// change, so a slider drag or a zoom becomes one undo step, not hundreds.
void App::recordHistory() {
    // (a playing camera path or a video export moves the view on its own: not the user's steps)
    if (dragButton >= 0 || flying || camAnim.active || camPath.playing || video.active || wheelPending != 0 || autopilot.active ||
        ImGui::IsAnyItemActive()) {
        history.changedAt = now;
        return;
    }
    std::string text = parText();
    if (text == history.lastText) {
        history.changedAt = -1;
        return;
    }
    if (history.changedAt < 0) history.changedAt = now;
    if (now - history.changedAt < 0.6) return;
    recordHistoryNow();  // (a new edit after undoing discards the redo tail)
    if (history.entries.size() > 200) {
        history.entries.erase(history.entries.begin());
        history.pos--;
    }
}

void App::recordHistoryNow() {
    std::string text = parText();
    if (history.pos >= 0 && history.entries[history.pos].second == text) return;
    if (history.pos + 1 < (int)history.entries.size()) history.entries.resize(history.pos + 1);
    history.entries.push_back({historyLabel(), text});
    history.pos = (int)history.entries.size() - 1;
    history.lastText = text;
    history.changedAt = -1;
}

void App::jumpToHistory(int i) {
    if (i < 0 || i >= (int)history.entries.size()) return;
    history.pos = i;
    loadParText(history.entries[i].second, "history", true);
}

void App::undo() {
    // A change that hasn't settled into the history yet is recorded first, so Redo
    // can bring it back; then Undo steps to the view before it.
    if (parText() != history.lastText && history.pos >= 0) recordHistoryNow();
    if (history.pos > 0) {
        jumpToHistory(history.pos - 1);
        toast("Undo", 0.8f);
    } else {
        toast("Nothing to undo", 1.0f);
    }
}

void App::redo() {
    if (history.pos + 1 < (int)history.entries.size()) {
        jumpToHistory(history.pos + 1);
        toast("Redo", 0.8f);
    } else {
        toast("Nothing to redo", 1.0f);
    }
}

// --self-test: exercises logic that normally needs keyboard/mouse input.
int App::selfTest() {
    int fails = 0;
    std::error_code ec;
    auto check = [&](bool ok, const char* what) {
        printf("self-test: %-58s %s\n", what, ok ? "ok" : "FAILED");
        if (!ok) fails++;
    };
    auto preset = [&](const char* name) { return loadPar(session.dataDir / "presets" / name); };
    history = History();
    recordHistoryNow();
    std::string start = parText();
    check(preset("10-menger-crystal.par"), "load a preset");
    recordHistoryNow();
    std::string menger = parText();
    check(preset("02-seahorse-valley.par"), "load a second preset");
    std::string seahorse = parText();
    // an unrecorded change: undo goes back to the last recorded view (Menger)
    undo();
    check(parText() == menger, "undo an unrecorded load returns to the previous view");
    undo();
    check(parText() == start, "undo again returns to the start");
    redo();
    check(parText() == menger, "redo");
    redo();
    check(parText() == seahorse, "redo brings back the change that wasn't recorded yet");
    undo();
    view.rs.exposure = 2.5f;  // a new edit after undo...
    recordHistoryNow();
    check(history.pos == (int)history.entries.size() - 1, "a new edit becomes the newest entry");
    redo();
    check(view.rs.exposure == 2.5f, "...and discards the redo tail");
    undo();
    check(parText() == menger, "undo the edit");
    // saved PARs restore the formula, camera and settings exactly
    std::string before = parText();
    loadParText(before, "roundtrip", true);
    check(parText() == before, "PAR text round-trips exactly");
    selectFormula("Spider");
    view.cs.formula = kCustomFormula;
    setMode(ViewMode::Classic2D);
    std::string withFormula = parText();
    selectFormula("Mandel");
    loadParText(withFormula, "formula", true);
    check(view.formulaName == "Spider" && rend.hasCustomFormula(), "a PAR restores its custom formula");
    // deep zoom: panning keeps every digit of the center, and PARs save them
    setMode(ViewMode::Classic2D);
    view.cs.formula = 0;
    view.cs.julia = false;
    view.cs.cx = 0, view.cs.cy = 1, view.cs.height = 1e-60;
    view.hpRe.clear();
    moveCenter(3e-61, -2e-61);
    check(view.hpRe.find("3") != std::string::npos && hp::diffOver(view.hpRe, "0", 1e-61, 400) > 2.99 &&
              hp::diffOver(view.hpIm, "1", 1e-61, 400) < -1.99,
          "panning at 1e-60 moves the exact center by 1e-61 steps");
    std::string deepPar = parText(), keepRe = view.hpRe;
    view.hpRe = "0";
    view.cs.cx = 5;
    loadParText(deepPar, "deep", true);
    check(view.hpRe == keepRe, "PAR files keep every digit of a deep-zoom center");
    // camera path from a deep keyframe out to a shallow one (review r2 1.2): near the end
    // the center must be most of the way to the shallow keyframe's, not stuck at the deep one
    camPath = CameraPath();
    camPath.keys.resize(2);
    camPath.keys[0].par = "mode = 2d\nclassic.center = 0 1\nclassic.centerHP = 0 1\nclassic.height = 1e-20\n";
    camPath.keys[1].par = "mode = 2d\nclassic.center = -0.6 0\nclassic.height = 3\n";
    camPath.keys[0].duration = 1;
    applyPathTime(0.99f);
    check(view.cs.cx < -0.3 && std::abs(hp::toDouble(view.hpRe) - view.cs.cx) < 1e-9,
          "a deep-to-shallow path heads for the shallow center");
    applyPathTime(1.0f);
    check(view.cs.cx == -0.6 && view.cs.height == 3.0, "...and ends exactly on it");
    // sun turns the short way, iteration limits interpolate in log space, easing, .f3dpath round trip
    camPath = CameraPath();
    camPath.keys.resize(2);
    camPath.keys[0].par = "mode = 2d\nlight.sunAzimuth = 350\nclassic.maxIter = 1000\n";
    camPath.keys[1].par = "mode = 2d\nlight.sunAzimuth = 10\nclassic.maxIter = 16000\n";
    camPath.keys[0].duration = 1;
    applyPathTime(0.5f);
    check((view.rs.sunAzimuth > 355 || view.rs.sunAzimuth < 5) && view.cs.maxIter > 3500 && view.cs.maxIter < 4500,
          "paths: the sun turns the short way; maxIter in log space");
    // a frame exactly on a middle keyframe shows that keyframe, not the path's end
    camPath.keys.resize(3);
    camPath.keys[2].par = "mode = 2d\nlight.sunAzimuth = 90\nclassic.maxIter = 16000\n";
    camPath.keys[1].duration = 1;
    applyPathTime(1.0f);
    check(std::abs(view.rs.sunAzimuth - 10) < 0.01f, "paths: a frame on a middle keyframe shows it");
    camPath.keys.resize(2);
    camPath.keys[0].ease = true;
    applyPathTime(0.25f);
    int eased = view.cs.maxIter;
    check(eased < 1600, "paths: an eased segment starts slowly");
    fs::path tmp = fs::temp_directory_path() / ("fract3d-selftest-" + std::to_string(getpid()) + ".f3dpath");
    savePath(tmp);
    camPath = CameraPath();
    check(loadPath(tmp) && camPath.keys.size() == 2 && camPath.keys[0].ease && !camPath.keys[1].ease, "paths: easing is saved");
    fs::remove(tmp, ec);
    endPathPreview();
    camPath = CameraPath();
    // custom formulas: right-click goes to the @julia partner with c = p1, and back
    loadParText("mode = 2d\nclassic.formula = 8\nformula.name = FnMandel\nformula.fn = 5 0 0 0\nclassic.center = 1 0.5\n", "julia", true);
    toggleJulia(-0.25, 0.75);
    check(view.formulaName == "FnJulia" && view.cs.formulaP[0][0] == -0.25f && view.cs.formulaP[0][1] == 0.75f && view.fn[0] == 5,
          "right-click on FnMandel shows FnJulia for that c (same fn1)");
    toggleJulia(0, 0);
    check(view.formulaName == "FnMandel" && view.cs.cx == 1.0 && view.cs.cy == 0.5 && view.fn[0] == 5,
          "...and right-click again returns to FnMandel's view");
    selectFormula("Spider");
    double cx0 = view.cs.cx;
    toggleJulia(0.3, 0.3);
    check(view.formulaName == "Spider" && view.cs.cx == cx0, "a formula without a Julia partner keeps its view");
    // Lift into 3D keeps the full-precision center, and Flatten brings it back
    loadParText("mode = 2d\nclassic.center = -0.743643887037151 0.13182590420533\nclassic.height = 1e-9\n", "lift", true);
    liftTo3D();
    const Param* lc = fractal().find("center");
    check(lc && lc->precise(0) == -0.743643887037151 && lc->precise(1) == 0.13182590420533, "Lift into 3D keeps every digit of the center");
    std::string lifted = parText();
    loadParText(lifted, "lifted", true);
    lc = fractal().find("center");
    check(lc && lc->precise(0) == -0.743643887037151, "...and so does its PAR");
    flattenTo2D();
    check(view.cs.cx == -0.743643887037151 && view.cs.cy == 0.13182590420533, "Flatten to 2D brings it back");
    // image-sequence names: exactly one %d / %0Nd, nothing printf could misread
    check(isImageSequence("out/f-%05d.png") && isImageSequence("f%d.png") && !isImageSequence("f-%05d.mp4") &&
              !isImageSequence("f%s%05d.png") && !isImageSequence("f%d%d.png") && !isImageSequence("plain.png"),
          "PNG sequence patterns are recognized safely");
    // progressive 2D rendering: with a tiny budget (one pass per step) every band must still be
    // started properly - a band begun right as the budget ran out used to stay black
    {
        IndexTarget t;
        Job2D j;
        j.active = true;
        j.cs = Classic2DSettings();
        j.cs.maxIter = 64;
        j.cs.fp64 = 0;
        j.chunk = 16;
        int w = 8192, h = 400;  // wide, so each band is only ~128 rows: several bands
        bool ok = t.ensure(w, h);
        if (ok) {
            t.clear();
            for (int n = 0; n < 10000 && !stepJob2D(j, t, 1e-9); n++) {}
            std::vector<float> v((size_t)w * h);
            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
            glGetTextureImage(t.value, 0, GL_RED, GL_FLOAT, (GLsizei)(v.size() * sizeof(float)), v.data());
            ok = std::none_of(v.begin(), v.end(), [](float x) { return x < -1.5f; });
        }
        t.release();
        check(ok, "a 2D render in the smallest steps leaves no band undrawn");
    }
    // series approximation: the series of a view, evaluated with a shift, describes a view
    // inside it as well as that view's own series does (the kernel uses it while zooming in)
    {
        using C = std::complex<double>;
        RefOrbitRequest rr;
        rr.re = "-0.743643887037158704752191506114774";
        rr.im = "0.131825904205311970493132056385139";
        rr.maxIter = 20000;
        rr.bits = 128;
        rr.bailout = 2;
        rr.dcMax = 1e-19;
        rr.blaEps = kBlaEpsilon;
        RefOrbitWorker rw;
        rw.request(rr);
        while (!rw.ready()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        SeriesRequest big, sub;
        big.orbit = sub.orbit = rw.orbitPtr();
        big.bla = sub.bla = rw.blaPtr();
        big.orbitVersion = sub.orbitVersion = rw.version();
        big.maxIter = sub.maxIter = 20000;
        big.bailout = sub.bailout = 2;
        big.dc0[0] = 3e-21, big.dc0[1] = -2e-21, big.half[0] = 1.6e-20, big.half[1] = 0.9e-20;
        sub.dc0[0] = big.dc0[0] + 0.4 * big.half[0], sub.dc0[1] = big.dc0[1] - 0.3 * big.half[1];
        sub.half[0] = big.half[0] / 3, sub.half[1] = big.half[1] / 3;
        SeriesWorker sw;
        sw.request(big);
        sw.wait();
        SeriesResult B = *sw.resultFor(big);
        sw.request(sub);
        sw.wait();
        SeriesResult S = *sw.resultFor(sub);
        const std::vector<double>& z = *rw.orbitPtr();
        double worst = 0;
        for (double fx : {-1.0, 1.0})
            for (double fy : {-1.0, 1.0}) {
                C v(fx * sub.half[0], fy * sub.half[1]), dc = C(sub.dc0[0], sub.dc0[1]) + v;
                auto eval = [&](const SeriesResult& r, C u) {
                    C e(0), up = u;
                    for (size_t p = 0; p < r.coef.size() / 2; p++, up *= u) e += C(r.coef[2 * p], r.coef[2 * p + 1]) * up;
                    return C(r.base[0], r.base[1]) + e;
                };
                C eb = eval(B, (v + C(sub.dc0[0] - big.dc0[0], sub.dc0[1] - big.dc0[1])) * B.invR);  // shifted, as the kernel does
                for (int n = B.skip; n < S.skip; n++) eb = 2.0 * C(z[2 * n], z[2 * n + 1]) * eb + eb * eb + dc;  // catch up
                C es = eval(S, v * S.invR);
                worst = std::max(worst, std::abs(eb - es) / std::abs(es));
            }
        check(B.skip > 0 && S.skip >= B.skip && worst < 1e-9, "a view's series, shifted, serves a view inside it");
    }
    // mouse-wheel zoom: eased notches add up exactly; a zoom covers a share of the gap to
    // the surface ahead (not to the orbit target) and never all of it
    wheelPending = 3;
    float total = 0;
    for (int i = 0; i < 400; i++) total += takeWheel(0.016f);
    check(std::abs(total - 3.0f) < 1e-4f && wheelPending == 0, "wheel notches are eased in and add up exactly");
    wheelPending = 500;
    takeWheel(0.016f);
    check(wheelPending <= 30, "a huge fling is capped");
    wheelPending = 0;
    view.cam.lookAt(Vec3(0, 0, -3), Vec3(0, 0, 0));
    deAtCam = 0.8f, centerHitT = 1.0f;
    zoom3D(1);
    check(std::abs(view.cam.pos.z - (-3 + 0.12f)) < 1e-4f && std::abs(view.cam.distance - 0.88f) < 1e-4f,
          "one notch: 12% of the way to the surface, which becomes the orbit target");
    deAtCam = 0.8f, centerHitT = 1.0f;
    float z0 = view.cam.pos.z;
    zoom3D(100);
    check(view.cam.pos.z - z0 <= 0.5f + 1e-5f, "a stalled frame never covers more than half the gap");
    deAtCam = -0.01f;
    z0 = view.cam.pos.z;
    zoom3D(1);
    check(view.cam.pos.z == z0, "no zooming further into a surface the camera touches");
    // hostile values are repaired
    loadParText("mode = 2d\nclassic.formula = 99\nclassic.maxIter = -3\npost.tonemap = 7\n", "bad", true);
    check(view.cs.formula < kClassicFormulaCount && view.cs.maxIter >= 1 && view.rs.tonemap <= 2, "hostile PAR values are sanitized");
    // the autopilot lets go whenever a hand is on the controls - but the autopilot tour,
    // which loads its own stops, keeps flying
    setMode(ViewMode::Fractal3D);
    engageAutopilot(0);
    check(autopilot.active && autopilot.style == FlightStyle::Around, "the autopilot engages in 3D mode");
    resetView();
    check(!autopilot.active, "resetting the view takes the controls from the autopilot");
    engageAutopilot(1);
    selectFractal(view.fractal, false);
    check(!autopilot.active, "switching fractal takes the controls");
    engageAutopilot(-1);
    loadParText(parText(), "self-test", true);
    check(!autopilot.active, "loading a view takes the controls");
    tourFlight.active = true;
    tourFlightNext();
    check(autopilot.active && tourFlight.active && view.mode == ViewMode::Fractal3D, "the autopilot tour flies on after loading a stop");
    view.rs.renderMode = 1;
    engageAutopilot(0);
    check(view.rs.renderMode == 0, "flying switches path tracing off");
    disengageAutopilot(nullptr);
    check(!tourFlight.active && view.rs.renderMode == 1, "letting go ends the tour and brings path tracing back");
    // a path-traced tour stop, then a real-time one: letting go keeps it real-time
    view.rs.renderMode = 1;
    engageAutopilot(0);
    autopilot.active = false;  // (as tourFlightNext does before the next stop)
    view.rs.renderMode = 0;
    engageAutopilot(0);
    disengageAutopilot(nullptr);
    check(view.rs.renderMode == 0, "a path-traced stop's restore doesn't carry over to a real-time one");
    // the ship keeps to the scale you've zoomed to, and to the clearance you set
    {
        const Fractal& f = fractal();
        float hint = f.autopilotClearance * f.sceneSize();
        probeValid = true;
        deAtCam = hint * 0.01f;
        view.cam.distance = hint * 0.02f;
        engageAutopilot(0);
        check(std::abs(autopilot.clearance - hint * 0.015f) < hint * 1e-4f, "zoomed in close, it circles at that scale");
        userClearance[f.key + "/around"] = 0.5f;
        engageAutopilot(0);
        check(std::abs(autopilot.clearance - 0.5f * f.sceneSize()) < 1e-4f, "the Clearance slider's value is remembered");
        userClearance.clear();
        disengageAutopilot(nullptr);
    }
    {   // the Mandelbox's inside is far finer than its outside: Through flies between two sizes
        int keep = view.fractal;
        selectFractal(lib_.indexOf("mandelbox"), true);
        probeValid = false;
        engageAutopilot(1);
        const Fractal& m = fractal();
        float S = m.sceneSize();
        check(std::abs(autopilot.clearance - m.autopilotInside * S) < 1e-5f * S &&
                  std::abs(autopilot.clearanceOpen - m.autopilotClearance * S) < 1e-5f * S,
              "the Mandelbox: small inside, its own scale outside");
        disengageAutopilot(nullptr);
        selectFractal(keep, true);
    }
    view.cam.roll = 0.4f;
    loadParText(parText(), "self-test", true);
    check(view.cam.roll == 0.0f, "a loaded view is level");
    // no usable normal while touching: it backs out the way it came, never stays stuck
    {
        Autopilot ap;
        ap.engage(FlightStyle::Through, Vec3(0, 0, 1), 1.0f, 1);
        ShipSensors s;
        s.valid = true;
        s.de = 0;
        s.eps = 1e-3f;
        s.normalValid = true;
        s.normal = Vec3(std::nanf(""), 0, 0);
        s.dirs = ap.whiskerDirs();
        s.free.assign(s.dirs.size(), 0.0f);
        Vec3 look, p = ap.step(0.016f, Vec3(0, 0, 0), s, look);
        check(p.z < 0 && std::isfinite(p.x), "touching with a broken normal: it reverses out");
    }
    printf("self-test: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}

// ------------------------------------------------------------------ session
// The current view is saved on exit (and every 30 s when it changed), and
// reopened at the next start unless something else was asked for.
void App::saveSession(bool force) {
    if (session.cli.hidden) return;
    if (!force && now - lastSessionSave < 30.0) return;
    lastSessionSave = now;
    std::string text = parText();
    if (text == lastSessionText) return;
    lastSessionText = text;
    std::ofstream(session.userDir / "session.par") << text;
}

// ------------------------------------------------------------------ preferences
// UI preferences (not part of a view, so not in PAR files).
void App::loadPrefs() {
    std::istringstream is(readTextFile((session.userDir / "prefs.ini").string()));
    std::string line;
    while (std::getline(is, line)) {
        size_t e = line.find('=');
        if (e == std::string::npos) continue;
        std::string k = line.substr(0, e);
        float v = std::strtof(line.c_str() + e + 1, nullptr);
        if (k == "theme") session.uiTheme = (int)v;
        else if (k == "uiScale") session.uiScale = std::clamp(v, 0.5f, 3.0f);
        else if (k == "showLearn") session.showLearn = v != 0;
        else if (k == "flySpeed") session.flySpeed = std::isfinite(v) ? std::clamp(v, 0.01f, 100.0f) : 1.5f;
        else if (k == "keepLighting") session.keepLighting = v != 0;
        else if (k == "restoreSession") session.restoreSession = v != 0;
        else if (k == "fullscreenMonitor") session.fullscreenMonitor = line.substr(e + 1);
        else
            visitRender(view.rs, [&](const char* name, const char*, auto* ptr, int, unsigned flags) {
                if ((flags & kPerf) && k == name) *ptr = static_cast<std::remove_reference_t<decltype(*ptr)>>(v);
            });
    }
    if (session.uiTheme != 0 && session.uiTheme != 1) session.uiTheme = 0;
    if (!std::isfinite(session.uiScale)) session.uiScale = 1.0f;
}

void App::savePrefs() {
    FILE* f = fopen((session.userDir / "prefs.ini").c_str(), "w");
    if (!f) return;
    fprintf(f, "theme=%d\nuiScale=%g\nshowLearn=%d\nflySpeed=%g\nkeepLighting=%d\nfullscreenMonitor=%s\nrestoreSession=%d\n",
            session.uiTheme, session.uiScale, (int)session.showLearn, session.flySpeed, (int)session.keepLighting,
            session.fullscreenMonitor.c_str(), (int)session.restoreSession);
    visitRender(view.rs, [&](const char* name, const char*, auto* ptr, int, unsigned flags) {
        if (flags & kPerf) fprintf(f, "%s=%g\n", name, (double)*ptr);
    });
    fclose(f);
}

// ------------------------------------------------------------------ helpers
void App::toast(const std::string& msg, float seconds) {
    // (a recording runs faster than real time, so a toast would hang over all of it)
    if (!recording()) {
        ui.toastMsg = msg;
        ui.toastUntil = glfwGetTime() + seconds;
    }
    printf("fract3d: %s\n", msg.c_str());
}

void App::applyPalette() {
    view.rs.palette = std::clamp(view.rs.palette, 0, (int)palettes.size() - 1);
    if (view.rs.palette == customPaletteIdx)
        palettes[customPaletteIdx] = makeCosinePalette("Custom (cosine editor)", view.cosine);
    if (view.rs.palette == gradientPaletteIdx)
        palettes[gradientPaletteIdx] = makeGradientPalette("Custom (gradient editor)", view.gradient);
    rend.setPalette(palettes[view.rs.palette]);
    paletteVersion++;
}

void App::selectFractal(int idx, bool reset) {
    view.fractal = std::clamp(idx, 0, (int)lib_.all().size() - 1);
    probeValid = false;
    disengageAutopilot("Autopilot off - you have the controls");
    Fractal& f = fractal();
    view.rs.stepFactor = f.hints.stepFactor;
    view.rs.detail = f.hints.detail;
    view.rs.maxSteps = f.hints.maxSteps;
    view.rs.maxDist = f.hints.maxDist;
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
    visitRender(view.rs, [&](const char*, const char*, auto* ptr, int n, unsigned flags) {
        if (!(flags & mask) || (session.keepLighting && (flags & kLighting))) return;
        size_t off = reinterpret_cast<const char*>(ptr) - reinterpret_cast<const char*>(&view.rs);
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
        visitRender(view.rs, [&](const char* name, const char* alias, auto* ptr, int n, unsigned flags) {
            if (!(flags & kLook) || (k != name && !(alias && k == alias))) return;
            known = true;
            if (session.keepLighting && (flags & kLighting)) return;
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
    view.rs.palette = pal;
    sanitize(view.rs);
    applyPalette();
}

void App::resetView() {
    Fractal& f = fractal();
    probeValid = false;
    view.cam.roll = 0;
    disengageAutopilot("Autopilot off - you have the controls");
    view.cam.lookAt(Vec3(f.camPos[0], f.camPos[1], f.camPos[2]), Vec3(f.camTarget[0], f.camTarget[1], f.camTarget[2]));
}

// ------------------------------------------------------------------ fullscreen
// Wayland doesn't expose window positions, so there the monitor comes from the
// user's choice (View > Fullscreen on) or the primary one; on X11 it's the
// monitor that shows most of the window.
GLFWmonitor* App::pickMonitor() {
    int n = 0;
    GLFWmonitor** mons = glfwGetMonitors(&n);
    if (n == 0) return nullptr;
    for (int i = 0; i < n; i++) {
        const char* name = glfwGetMonitorName(mons[i]);
        if (name && session.fullscreenMonitor == name) return mons[i];
    }
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) return glfwGetPrimaryMonitor();
    int wx, wy, ww, wh;
    glfwGetWindowPos(win, &wx, &wy);
    glfwGetWindowSize(win, &ww, &wh);
    GLFWmonitor* best = glfwGetPrimaryMonitor();
    long bestArea = -1;
    for (int i = 0; i < n; i++) {
        int mx, my, mw, mh;
        glfwGetMonitorWorkarea(mons[i], &mx, &my, &mw, &mh);
        long ox = std::max(0, std::min(wx + ww, mx + mw) - std::max(wx, mx));
        long oy = std::max(0, std::min(wy + wh, my + mh) - std::max(wy, my));
        if (ox * oy > bestArea) bestArea = ox * oy, best = mons[i];
    }
    return best;
}

void App::toggleFullscreen() {
    bool wayland = glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
    if (!fullscreen) {
        GLFWmonitor* mon = pickMonitor();
        if (!mon) return;
        if (!wayland) glfwGetWindowPos(win, &savedWin[0], &savedWin[1]);  // not available on Wayland
        glfwGetWindowSize(win, &savedWin[2], &savedWin[3]);
        const GLFWvidmode* vm = glfwGetVideoMode(mon);
        glfwSetWindowMonitor(win, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
        fullscreen = true;
    } else {
        glfwSetWindowMonitor(win, nullptr, savedWin[0], savedWin[1], savedWin[2], savedWin[3], 0);
        fullscreen = false;
    }
}

void App::setMode(ViewMode m) {
    if (m != view.mode) {
        setMouseCapture(false);
        dragButton = -1;
        wheelPending = 0;
        if (m != ViewMode::Fractal3D) disengageAutopilot(nullptr);
    }
    view.mode = m;
    shownSig2D.clear();
    job2D.active = false;
    lastSig3D.clear();
}

// ------------------------------------------------------------------ frame
void App::frame() {
    // Nothing can change on screen until the user does something: sleep until an
    // event arrives (waking now and then so edited shader files still reload).
    if (idleWait > 0) glfwWaitEventsTimeout(idleWait);
    else glfwPollEvents();
    now = glfwGetTime();
    double rawDt = now - lastFrameTime;
    dt = (float)std::min(rawDt, 0.1);  // animation/motion step, clamped after stalls
    if (session.cli.fixedDt > 0) dt = std::min(session.cli.fixedDt, 0.1f);
    lastFrameTime = now;
    fps = fps * 0.95f + (rawDt > 0 ? (float)(1.0 / rawDt) : 0.0f) * 0.05f;
    if (session.cli.fixedDt > 0) fps = 1.0f / dt;  // (recordings and tests: the rate they play at, not how fast they ran)
    glfwGetFramebufferSize(win, &fbW, &fbH);
    glfwGetWindowSize(win, &winW, &winH);
    if (fbW <= 0 || fbH <= 0) {  // minimized
        glfwWaitEventsTimeout(0.1);
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    if (session.cli.fakeMouse[0] >= 0) {
        float mx = session.cli.fakeMouse[0], my = session.cli.fakeMouse[1];
        if (session.cli.mouseTo[0] >= 0) {  // --mouse-to: glide across the frames, easing in and out
            float t = std::clamp((float)frameCount / std::max(session.cli.uiShotFrames - 1, 1), 0.0f, 1.0f);
            t = t * t * (3 - 2 * t);
            mx += (session.cli.mouseTo[0] - mx) * t;
            my += (session.cli.mouseTo[1] - my) * t;
        }
        ImGui::GetIO().AddMousePosEvent(mx, my);
    }
    ImGui::NewFrame();

    handleKeys();
    updatePathPlayback();
    pollVideoEncoder();
    for (auto& p : droppedFiles) openDroppedFile(p);
    droppedFiles.clear();
    if (view.mode == ViewMode::Fractal3D) input3D(dt);
    else input2D(dt);
    inputGamepad(dt);
    if (camAnim.active) {
        float t = std::min((float)(now - camAnim.start) / 0.45f, 1.0f);
        float s = t * t * (3.0f - 2.0f * t);
        float dyaw = std::remainder(camAnim.yaw1 - camAnim.yaw0, 6.2831853f);  // shortest way round
        view.cam.yaw = camAnim.yaw0 + dyaw * s;
        view.cam.pitch = camAnim.pitch0 + (camAnim.pitch1 - camAnim.pitch0) * s;
        view.cam.distance = camAnim.dist0 * std::pow(camAnim.dist1 / camAnim.dist0, s);
        if (t >= 1.0f || dragButton >= 0) camAnim.active = false;
    }
    if (session.cli.autopilot == 3) {  // --autopilot tour
        tourFlight.active = true;
        tourFlightNext();
        session.cli.autopilot = -1;
    } else if (session.cli.autopilot >= 0 && (probeValid || view.mode != ViewMode::Fractal3D)) {
        // --autopilot: as if G were pressed once the view is measured (so it flies at the
        // scale it finds, like the key does); in 2D mode it says how to get to 3D
        engageAutopilot(session.cli.autopilot == 2 ? -1 : session.cli.autopilot);
        session.cli.autopilot = -1;
    }
    if (autopilot.active) {
        // any hand on the controls takes over, as in an aircraft
        if (flying || dragButton >= 0 || wheelPending != 0 || camAnim.active || camPath.playing || video.active)
            disengageAutopilot("Autopilot off - you have the controls");
        else if (view.mode == ViewMode::Fractal3D && !poster.active)
            flyAutopilot(dt);
    }

    // live reload of shaders/, fractals/ and the Learn docs (edit a file and save)
    if (now - lastReloadCheck > 0.5) {
        lastReloadCheck = now;
        refreshDocs();
        recordHistory();
        saveSession(false);
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
    if (view.rs.cycleSpeed != 0.0f) cycleOffset = std::fmod(cycleOffset + view.rs.cycleSpeed * dt + 256.0f, 256.0f);

    rend.timer.poll();
    if (poster.active) updatePoster();
    else if (view.mode == ViewMode::Fractal3D) render3D();
    else {
        render2D();
        bool busy2D = job2D.active || refPending;  // (a job that finished within the frame never counts)
        if (!busy2D) busySince2D = -1;
        else if (busySince2D < 0) busySince2D = now;
        updateJuliaInset();
    }
    if (view.mode == ViewMode::Fractal3D) updateProbe();
    updateCockpit(dt);

    // present
    GLuint target = 0;
    if (!session.cli.uiShotPath.empty() || recording()) {
        uiShotRT.ensure(fbW, fbH, GL_RGBA8, GL_NEAREST);
        target = uiShotRT.fbo;
    }
    rend.display(view.mode, &rend.accum, &rend.index2D, view.rs, view.cs, cycleOffset, fbW, fbH, target);

    drawUI();
    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, fbW, fbH);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (recording()) {  // --ui-record: every frame from --record-from on, as it appears, into ffmpeg
        int f = frameCount++;
        if (f >= session.cli.recordFrom) {
            if (!recordPipe) {
                float fps = session.cli.fixedDt > 0 ? 1.0f / session.cli.fixedDt : 30.0f;
                char cmd[1024];
                snprintf(cmd, sizeof cmd,
                         "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s %dx%d -framerate %.4f -i - -vf vflip "
                         "-c:v libx264 -preset medium -crf 14 -pix_fmt yuv420p -movflags +faststart '%s'",
                         fbW, fbH, fps, session.cli.uiRecord.c_str());
                recordPipe = popen(cmd, "w");
                if (!recordPipe) {
                    fprintf(stderr, "fract3d: can't start ffmpeg for --ui-record\n");
                    exitCode = 1;
                    quit = true;
                }
            }
            if (recordPipe) {
                std::vector<uint8_t> px((size_t)fbW * fbH * 4);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
                fwrite(px.data(), 1, px.size(), recordPipe);
            }
        }
        if (frameCount >= session.cli.uiShotFrames) {
            if (recordPipe && pclose(recordPipe) != 0) exitCode = 1;
            recordPipe = nullptr;
            printf("fract3d: recorded %d frames into %s\n", std::max(0, frameCount - session.cli.recordFrom), session.cli.uiRecord.c_str());
            quit = true;
        }
    }
    if (!session.cli.uiShotPath.empty() && ++frameCount >= session.cli.uiShotFrames) {
        std::vector<uint8_t> px((size_t)fbW * fbH * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
        writePngRaw(session.cli.uiShotPath, fbW, fbH, px.data());
        printf("fract3d: ui-shot: samples=%d scale=%.2f perSampleFull=%.2fms deAtCam=%g centerHitT=%g camDist=%g\n", samples,
               (float)rend.accum.w / std::max(fbW, 1), perSampleMsFull, deAtCam, centerHitT, view.cam.distance);
        uiShotRT.release();
        quit = true;
    }
    glfwSwapBuffers(win);
    idleWait = computeIdle();
}

// How long the next frame may wait for input: 0 while anything is still being drawn
// or animated. The long wait still wakes now and then so edited files reload.
double App::computeIdle() {
    if (session.cli.hidden) return 0;  // headless renders and tests run flat out
    if (poster.active || job2D.active || camAnim.active || dragButton >= 0 || flying || gamepadActive || camPath.playing || wheelPending != 0 ||
        autopilot.active || tourFlight.active ||
        video.active || refPending || (inset.job.active && view.mode == ViewMode::Classic2D))
        return 0;
    if (view.rs.cycleSpeed != 0.0f || glfwGetTime() < ui.toastUntil) return 0;
    if (!animPaused)
        for (auto& p : fractal().params)
            if (p.animate) return 0;
    if (view.mode == ViewMode::Fractal3D) {
        if (rend.status(fractal()) == Renderer::ProgStatus::Compiling) return 0;
        if (interactive || samples < (view.rs.renderMode ? view.rs.maxSamplesPT : view.rs.maxSamplesRT)) return 0;
    } else if (interactive) {
        return 0;
    }
    if (video.finishing) return 0.1;  // waiting for ffmpeg to exit
    for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; j++)
        if (glfwJoystickIsGamepad(j)) return 0.05;  // gamepads are polled, not event-driven: check often
    return 0.25;
}

// ------------------------------------------------------------------ keys
void App::handleKeys() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    bool ctrl = io.KeyCtrl;

    if (pressed(ImGuiKey_Tab)) {
        if (io.KeyShift) {
            ui.showPanels = !ui.showPanels;
            if (!ui.showPanels) toast("Panels hidden - Shift+Tab brings them back", 2.0f);
        } else {
            ui.showUI = !ui.showUI;
        }
    }
    if (pressed(ImGuiKey_F1)) ui.showHelp = !ui.showHelp;
    if (pressed(ImGuiKey_F12)) takeScreenshot();
    if (pressed(ImGuiKey_L)) session.showLearn = !session.showLearn;
    if (pressed(ImGuiKey_M)) setMode(view.mode == ViewMode::Fractal3D ? ViewMode::Classic2D : ViewMode::Fractal3D);
    if (pressed(ImGuiKey_Escape) && ui.showHelp) ui.showHelp = false;
    if (ctrl && pressed(ImGuiKey_S)) saveNamedPar();
    if (ctrl && pressed(ImGuiKey_Q)) quit = true;
    if (ctrl && pressed(ImGuiKey_Z)) io.KeyShift ? redo() : undo();
    if (ctrl && pressed(ImGuiKey_Y)) redo();
    if (pressed(ImGuiKey_N)) tourStep(io.KeyShift ? -1 : 1);
    if (pressed(ImGuiKey_K) && !ctrl) addKeyframe();
    if (pressed(ImGuiKey_C) && !ctrl) {  // Fractint's 'c': color cycling
        if (view.rs.cycleSpeed != 0.0f) lastCycleSpeed = view.rs.cycleSpeed, view.rs.cycleSpeed = 0.0f;
        else view.rs.cycleSpeed = lastCycleSpeed;
        toast(view.rs.cycleSpeed != 0.0f ? "Color cycling on" : "Color cycling off", 1.2f);
    }
    if (pressed(ImGuiKey_F11)) toggleFullscreen();

    if (view.mode == ViewMode::Fractal3D) {
        if (pressed(ImGuiKey_P)) {
            view.rs.renderMode = 1 - view.rs.renderMode;
            toast(view.rs.renderMode ? "Path tracing: hold still to refine" : "Real-time rendering", 1.5f);
        }
        if (pressed(ImGuiKey_R) && !ctrl) resetView();
        if (pressed(ImGuiKey_X) && !ctrl) cockpit.show = !cockpit.show;
        if (pressed(ImGuiKey_G) && !ctrl) {
            if (io.KeyShift) engageAutopilot(autopilot.active ? 1 - (int)autopilot.style : 1 - fractal().autopilotStyle);
            else if (autopilot.active) disengageAutopilot("Autopilot off");
            else engageAutopilot(-1);
        }
        if (pressed(ImGuiKey_Space)) animPaused = !animPaused;
        if (pressed(ImGuiKey_F) && centerHitT > 0) {
            view.rs.autoFocus = 0;
            view.rs.focusDist = centerHitT;
            toast("Focus set to the surface at screen center", 1.5f);
        }
        for (int k = 0; k < 9; k++)
            if (pressed((ImGuiKey)(ImGuiKey_1 + k)) && k < (int)lib_.all().size()) selectFractal(k, true);
    } else {
        if (pressed(ImGuiKey_O)) view.cs.showOrbit = !view.cs.showOrbit;
        if (pressed(ImGuiKey_J)) ui.showJuliaInset = !ui.showJuliaInset;
        if (pressed(ImGuiKey_B)) view.cs.banded = !view.cs.banded;
        if (pressed(ImGuiKey_Home)) {
            view.cs.julia = 0;
            view.cs.cx = view.cs.formula == 4 ? 0.0 : -0.6;
            view.cs.cy = 0;
            view.cs.height = 3.0;
        }
        if (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd)) view.cs.maxIter = std::min(view.cs.maxIter * 2, kMaxIterations);
        if (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract)) view.cs.maxIter = std::max(view.cs.maxIter / 2, 16);
    }
}

// ------------------------------------------------------------------ 3D input
// While orbiting or looking, the cursor is hidden and locked (with raw motion
// when available), so a drag isn't stopped by the edge of the screen. Deltas
// then come straight from GLFW, and ImGui ignores the mouse until release.
void App::setMouseCapture(bool on) {
    if (on == mouseCaptured) return;
    mouseCaptured = on;
    ImGuiIO& io = ImGui::GetIO();
    if (on) {
        glfwGetCursorPos(win, &captureLast[0], &captureLast[1]);
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        glfwGetCursorPos(win, &captureLast[0], &captureLast[1]);
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    } else {
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }
}

void App::input3D(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    bool overUI = io.WantCaptureMouse;
    if (!mouseCaptured)
        for (int b = 0; b < 3; b++)
            if (ImGui::IsMouseClicked(b) && !overUI && dragButton < 0) {
                dragButton = b;
                pressX = io.MousePos.x;
                pressY = io.MousePos.y;
            }
    // releases are read from GLFW, because ImGui doesn't see the mouse while it's captured
    if (dragButton >= 0 && glfwGetMouseButton(win, dragButton) != GLFW_PRESS && !ImGui::IsMouseDown(dragButton)) {
        dragButton = -1;
        setMouseCapture(false);
    }
    if (!mouseCaptured && ImGui::IsMouseDoubleClicked(0) && !overUI) {
        pickRequested = true;
        pickX = io.MousePos.x;
        pickY = io.MousePos.y;
    }

    float dx = io.MouseDelta.x, dy = io.MouseDelta.y;
    if (mouseCaptured) {
        double x, y;
        glfwGetCursorPos(win, &x, &y);
        dx = (float)(x - captureLast[0]);
        dy = (float)(y - captureLast[1]);
        captureLast[0] = x;
        captureLast[1] = y;
    } else if ((dragButton == 0 && !io.KeyShift) || dragButton == 1) {
        // start capturing once the drag really moves (so double-clicks still work)
        if (std::abs(io.MousePos.x - pressX) + std::abs(io.MousePos.y - pressY) > 4 && !session.cli.hidden) setMouseCapture(true);
    }
    if (std::abs(dx) > 500 || std::abs(dy) > 500) dx = dy = 0;  // first frame after focus
    float tanHalf = std::tan(view.rs.fov * 0.5f * 0.0174533f);
    if (dragButton == 0 && !io.KeyShift) view.cam.orbit(dx * 0.006f, -dy * 0.006f);
    else if (dragButton == 1) view.cam.look(dx * 0.003f, -dy * 0.003f);
    else if (dragButton == 2 || (dragButton == 0 && io.KeyShift)) {
        float s = 2.0f * tanHalf / std::max(winH, 1);
        view.cam.pan(-dx * s, dy * s);
    }
    if (!overUI) wheelPending += io.MouseWheel;
    if (probeValid) {  // (zooming needs to know where the surfaces are: after a jump, wait for the probe)
        if (float w = takeWheel(dt)) zoom3D(w);
    }

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
            float base = deAtCam > 0 ? deAtCam : view.cam.distance * 0.01f;
            base = std::max(base, view.cam.distance * 1e-4f);
            float speed = base * session.flySpeed * (io.KeyShift ? 4.0f : 1.0f);
            view.cam.move(d.normalized(), speed * dt);
            // keep the orbit target on whatever surface is ahead, so orbiting feels right after flying
            if (centerHitT > 0) view.cam.setTargetDistance(view.cam.distance + (centerHitT - view.cam.distance) * std::min(dt * 4.0f, 1.0f));
        }
    }
}

// ------------------------------------------------------------------ gamepad
// Any controller GLFW knows (Xbox layout names): left stick moves, right stick
// looks, triggers go down/up, bumpers slow/fast. A: path tracing, B: hide UI,
// X: screenshot, Y: next tour stop, D-pad left/right: switch fractal.
void App::inputGamepad(float dt) {
    GLFWgamepadstate st{};
    int pad = -1;
    for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST && pad < 0; j++)
        if (glfwJoystickIsGamepad(j) && glfwGetGamepadState(j, &st)) pad = j;
    if (pad < 0) {
        gamepadActive = false;
        return;
    }
    auto axis = [&](int a) {
        float v = st.axes[a];
        return std::abs(v) < 0.15f ? 0.0f : (v - std::copysign(0.15f, v)) / 0.85f;  // dead zone
    };
    auto pressed = [&](int b) { return st.buttons[b] == GLFW_PRESS && prevPad.buttons[b] != GLFW_PRESS; };
    float lx = axis(GLFW_GAMEPAD_AXIS_LEFT_X), ly = axis(GLFW_GAMEPAD_AXIS_LEFT_Y);
    float rx = axis(GLFW_GAMEPAD_AXIS_RIGHT_X), ry = axis(GLFW_GAMEPAD_AXIS_RIGHT_Y);
    float up = (st.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1) * 0.5f, down = (st.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1) * 0.5f;
    float boost = st.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] ? 4.0f : st.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] ? 0.25f : 1.0f;
    gamepadActive = lx || ly || rx || ry || up > 0.05f || down > 0.05f;

    if (view.mode == ViewMode::Fractal3D) {
        view.cam.look(rx * 2.2f * dt, -ry * 2.2f * dt);
        Vec3 d(lx, up - down, -ly);
        if (d.length() > 0.01f) {
            flying = true;
            float base = deAtCam > 0 ? deAtCam : view.cam.distance * 0.01f;
            base = std::max(base, view.cam.distance * 1e-4f);
            view.cam.move(d, base * session.flySpeed * boost * dt);
            if (centerHitT > 0) view.cam.setTargetDistance(view.cam.distance + (centerHitT - view.cam.distance) * std::min(dt * 4.0f, 1.0f));
        }
        if (pressed(GLFW_GAMEPAD_BUTTON_A)) view.rs.renderMode = 1 - view.rs.renderMode;
        if (pressed(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT)) selectFractal((view.fractal + 1) % (int)lib_.all().size(), true);
        if (pressed(GLFW_GAMEPAD_BUTTON_DPAD_LEFT))
            selectFractal((view.fractal + (int)lib_.all().size() - 1) % (int)lib_.all().size(), true);
    } else {
        // 2D: left stick pans (a quarter screen per second), right stick up/down zooms
        moveCenter(lx * view.cs.height * 0.5 * boost * dt, -ly * view.cs.height * 0.5 * boost * dt);
        if (ry != 0.0f) view.cs.height = std::clamp(view.cs.height * std::pow(2.0, ry * 1.5 * boost * dt), kMinHeight, 50.0);
    }
    if (pressed(GLFW_GAMEPAD_BUTTON_B)) ui.showUI = !ui.showUI;
    if (pressed(GLFW_GAMEPAD_BUTTON_X)) takeScreenshot();
    if (pressed(GLFW_GAMEPAD_BUTTON_Y)) tourStep(1);
    if (pressed(GLFW_GAMEPAD_BUTTON_START)) setMode(view.mode == ViewMode::Fractal3D ? ViewMode::Classic2D : ViewMode::Fractal3D);
    prevPad = st;
}

// ------------------------------------------------------------------ 2D input
void App::input2D(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    bool overUI = io.WantCaptureMouse || !ImGui::IsMousePosValid();
    float sx = (float)fbW / std::max(winW, 1), sy = (float)fbH / std::max(winH, 1);
    double ps = view.cs.height / fbH;
    double mx = io.MousePos.x * sx, my = io.MousePos.y * sy;
    double px = view.cs.cx + (mx - fbW * 0.5) * ps, py = view.cs.cy + (fbH * 0.5 - my) * ps;
    if (!ImGui::IsMousePosValid()) px = view.cs.cx, py = view.cs.cy;  // keyboard zoom without a mouse: zoom at center

    for (int b = 0; b < 3; b++)
        if (ImGui::IsMouseClicked(b) && !overUI && dragButton < 0) {
            dragButton = b;
            pressX = io.MousePos.x;
            pressY = io.MousePos.y;
        }
    if (dragButton == 0 || dragButton == 2) {
        if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) moveCenter(-io.MouseDelta.x * sx * ps, io.MouseDelta.y * sy * ps);
    }
    if (dragButton == 1 && ImGui::IsMouseReleased(1)) {
        if (std::abs(io.MousePos.x - pressX) + std::abs(io.MousePos.y - pressY) < 5) toggleJulia(px, py);
    }
    if (dragButton >= 0 && !ImGui::IsMouseDown(dragButton)) dragButton = -1;
    if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Space, false) && !overUI) toggleJulia(px, py);

    if (!overUI) wheelPending += io.MouseWheel;
    float wheel = takeWheel(dt);
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyDown(ImGuiKey_PageUp)) wheel += dt * 4;
        if (ImGui::IsKeyDown(ImGuiKey_PageDown)) wheel -= dt * 4;
    }
    if (wheel != 0.0f) {
        // clamp the zoom factor first, so hitting a limit doesn't slide the view
        double newH = std::clamp(view.cs.height * std::pow(0.8, wheel), kMinHeight, 50.0);
        double f = newH / view.cs.height;
        // the point under the cursor stays put: move the center toward it by (1 - f)
        // of the offset - a relative amount, so deep zooms keep full precision
        double offX = ImGui::IsMousePosValid() ? (mx - fbW * 0.5) * ps : 0.0;
        double offY = ImGui::IsMousePosValid() ? (fbH * 0.5 - my) * ps : 0.0;
        moveCenter(offX * (1 - f), offY * (1 - f));
        view.cs.height = newH;
    }
}

// Right-click / Space in 2D: the Julia set for the point (px, py), and back.
void App::toggleJulia(double px, double py) {
    if (view.cs.formula == 4) {
        toast("Newton's method has no Julia/Mandelbrot pair", 2);
        return;
    }
    if (view.cs.formula == kCustomFormula) {
        // A formula's Julia sets are another formula (named by "; @julia = Name"),
        // with the clicked point as p1 - Fractint's formula files worked the same way.
        auto& ps = view.cs.formulaP;
        if (!juliaReturn.formula.empty()) {
            std::string back = juliaReturn.formula;
            juliaReturn.formula.clear();
            selectFormula(back);
            for (int i = 0; i < kFormulaParams; i++) ps[i][0] = juliaReturn.p[i][0], ps[i][1] = juliaReturn.p[i][1];
            std::copy(juliaReturn.fn, juliaReturn.fn + 4, view.fn);
            compileFormula();
            view.cs.cx = savedMandel[0], view.cs.cy = savedMandel[1], view.cs.height = savedMandel[2];
            if (!savedMandelHP[0].empty()) {
                view.hpRe = savedMandelHP[0], view.hpIm = savedMandelHP[1];
                view.hpShadow[0] = view.cs.cx, view.hpShadow[1] = view.cs.cy;
            }
            toast("Back to " + back);
            return;
        }
        const FormulaDef* d = findFormula(view.formulaName);
        if (!d || d->julia.empty() || !findFormula(d->julia)) {
            toast("This formula has no Julia partner. Add a line  ; @julia = OtherFormula  before it in its .frm file", 5);
            return;
        }
        syncCenter();
        savedMandel[0] = view.cs.cx, savedMandel[1] = view.cs.cy, savedMandel[2] = view.cs.height;
        savedMandelHP[0] = view.hpRe, savedMandelHP[1] = view.hpIm;
        juliaReturn.formula = view.formulaName;
        for (int i = 0; i < kFormulaParams; i++) juliaReturn.p[i][0] = ps[i][0], juliaReturn.p[i][1] = ps[i][1];
        std::copy(view.fn, view.fn + 4, juliaReturn.fn);
        std::string partner = d->julia;
        selectFormula(partner);  // its own @view and defaults...
        std::copy(juliaReturn.fn, juliaReturn.fn + 4, view.fn);  // ...but the same functions
        view.cs.formulaP[0][0] = (float)px, view.cs.formulaP[0][1] = (float)py;
        compileFormula();
        char buf[160];
        snprintf(buf, sizeof buf, "%s for c = p1 = %.6f %+.6fi (right-click again to go back)", partner.c_str(), px, py);
        toast(buf, 4);
        return;
    }
    if (!view.cs.julia) {
        syncCenter();
        savedMandel[0] = view.cs.cx;
        savedMandel[1] = view.cs.cy;
        savedMandelHP[0] = view.hpRe;
        savedMandelHP[1] = view.hpIm;
        savedMandel[2] = view.cs.height;
        view.cs.jx = px;
        view.cs.jy = view.cs.formula == 1 ? -py : py;
        view.cs.julia = 1;
        view.cs.cx = 0;
        view.cs.cy = 0;
        view.cs.height = 3.2;
        char buf[128];
        snprintf(buf, sizeof buf, "Julia set for c = %.6f %+.6fi", view.cs.jx, view.cs.jy);
        toast(buf);
    } else {
        view.cs.julia = 0;
        view.cs.cx = savedMandel[0];
        view.cs.cy = savedMandel[1];
        if (!savedMandelHP[0].empty()) {  // back to the exact deep-zoom center
            view.hpRe = savedMandelHP[0];
            view.hpIm = savedMandelHP[1];
            view.hpShadow[0] = view.cs.cx;
            view.hpShadow[1] = view.cs.cy;
        }
        view.cs.height = savedMandel[2];
        toast("Back to the parameter plane");
    }
}

// ------------------------------------------------------------------ mouse-wheel zoom
// Each notch is eased in over about a quarter of a second instead of jumping.
float App::takeWheel(float dt) {
    if (wheelPending == 0.0f) return 0.0f;
    wheelPending = std::clamp(wheelPending, -30.0f, 30.0f);  // a fast fling, not a queue of hundreds
    float take = wheelPending * (1.0f - std::exp(-dt * 14.0f));
    if (std::abs(wheelPending - take) < 0.002f) take = wheelPending;
    wheelPending -= take;
    return take;
}

// Zooming in covers a fraction of the way to the surface at the center of the screen
// (measured by the probe), so the camera slows down near surfaces and never passes
// through one, and the orbit target moves onto that surface - the same as flying
// forward with W. With nothing ahead it flies forward within the empty space around
// the camera, target and all, so the scene's scale (which sets how far rays reach)
// never collapses. Zooming out is limited by that empty space too, so it can't back
// into a wall.
void App::zoom3D(float notches) {
    Camera& c = view.cam;
    float f = std::pow(0.88f, std::abs(notches));  // 12% per notch
    if (notches > 0) {
        if (deAtCam <= 0 || centerHitT == 0) return;  // touching (or inside) a surface: no further in
        bool surface = centerHitT > 0;
        // (at most half the remaining gap per frame: a stalled frame catching up on many
        // notches at once must not land on - or, with a stale probe, past - the surface)
        float s = (surface ? centerHitT : deAtCam) * std::min(1.0f - f, 0.5f);
        c.pos += c.forward() * s;
        if (surface) {
            centerHitT -= s;  // (until the probe measures again)
            c.setTargetDistance(std::max(centerHitT, 1e-7f));
        }
        deAtCam -= s;
    } else {
        float s = c.distance * (1.0f / f - 1.0f);
        if (deAtCam > 0) s = std::min(s, deAtCam * 0.8f);  // don't back into anything behind us
        c.pos += c.forward() * -s;
        c.setTargetDistance(c.distance + s);
        deAtCam += s;
    }
}

// ------------------------------------------------------------------ 3D rendering
View3D App::makeView(int w, int h) const {
    View3D v;
    v.pos = view.cam.pos;
    v.fwd = view.cam.forward();
    v.right = view.cam.right();
    v.up = view.cam.up();
    if (view.cam.roll != 0.0f) {  // bank: turn the image around the view direction
        float c = std::cos(view.cam.roll), s = std::sin(view.cam.roll);
        Vec3 r = v.right * c + v.up * s, u = v.up * c - v.right * s;
        v.right = r;
        v.up = u;
    }
    v.tanHalfFov = std::tan(view.rs.fov * 0.5f * 0.0174533f);
    v.sceneScale = view.cam.distance;
    v.focusDist = view.rs.autoFocus ? (centerHitT > 0 ? centerHitT : view.cam.distance) : view.rs.focusDist;
    v.time = (float)now;
    v.animTime = animTime;
    v.fullW = w;
    v.fullH = h;
    std::copy(&view.cs.formulaP[0][0], &view.cs.formulaP[0][0] + 10, &v.formulaP[0][0]);
    if (const Param* it = lib_.all()[view.fractal].find("iterations")) v.formulaMaxit = it->value[0];
    return v;
}

template <class T>
static void appendBytes(std::vector<uint8_t>& v, const T& x) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&x);
    v.insert(v.end(), p, p + sizeof(T));
}

std::vector<uint8_t> App::signature3D(int w, int h) const {
    RenderSettings r = view.rs;
    // palette cycling only matters when the palette is used for coloring
    if (r.colorMode <= 2) r.colorOffset = view.rs.colorOffset + cycleOffset / 256.0f;
    // the focus only matters with a lens
    if (r.aperture <= 0.0f) r.focusDist = 0, r.autoFocus = false;
    else if (r.autoFocus) r.focusDist = 0;  // autofocus distance is added (quantized) below
    std::vector<uint8_t> s;
    s.reserve(512);
    appendFields(s, r, kTrace3D, [](RenderSettings& x, auto&& f) { visitRender(x, f); });
    appendBytes(s, view.rs.palette);
    appendBytes(s, view.cam.pos);
    appendBytes(s, view.cam.yaw);
    appendBytes(s, view.cam.pitch);
    appendBytes(s, view.cam.roll);
    appendBytes(s, view.cam.distance);
    appendBytes(s, view.fractal);
    appendBytes(s, generation);
    appendBytes(s, formulaGeneration);  // the landscape can use the user formula...
    appendBytes(s, view.cs.formulaP);   // ...and its parameters
    appendBytes(s, paletteVersion);
    appendBytes(s, w);
    appendBytes(s, h);
    if (view.rs.aperture > 0.0f && view.rs.autoFocus) {
        // quantize so probe noise doesn't restart accumulation every frame
        float fd = makeView(w, h).focusDist;
        int q = (int)std::lround(std::log(std::max(fd, 1e-9f)) * 200.0f);
        appendBytes(s, q);
    }
    for (auto& p : lib_.all()[view.fractal].params) {
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
    float targetMs = 1000.0f / std::max(view.rs.targetFps, 10.0f) * 0.8f;
    // Another fractal or render mode can cost a hundred times more per sample: until it's
    // measured, assume it's expensive (a Kleinian path-traced at full size on a stale cheap
    // estimate queued seconds of GPU work and stalled the desktop).
    if (view.fractal != kind3DFractal || view.rs.renderMode != kind3DMode) {
        kind3DFractal = view.fractal;
        kind3DMode = view.rs.renderMode;
        perSampleMsFull = std::max(perSampleMsFull, 60.0f);
    }
    if (view.rs.adaptiveRes) motionScale = std::clamp(std::sqrt(targetMs / std::max(perSampleMsFull, 0.01f)), std::min(0.2f, view.rs.stillScale), view.rs.stillScale);
    else motionScale = view.rs.stillScale;

    float scale = interactive ? motionScale : view.rs.stillScale;
    // snap to steps so small estimate changes don't reallocate the buffer
    scale = std::clamp(std::round(scale * 20.0f) / 20.0f, 0.1f, 2.0f);
    int rw = std::max(1, (int)(fbW * scale)), rh = std::max(1, (int)(fbH * scale));
    if (rend.accum.w != rw || rend.accum.h != rh) {
        rend.accum.ensure(rw, rh, GL_RGBA32F, GL_LINEAR);
        samples = 0;
        band3DRow = 0;
    }
    int maxS = view.rs.renderMode ? view.rs.maxSamplesPT : view.rs.maxSamplesRT;
    if (samples >= maxS) return;  // converged: GPU idles
    // Samples whose cost isn't measured yet don't pile up: with two frames' worth in
    // flight, wait for a result (the estimate may be far too low for this view). This
    // comes before clearing: skipping a frame must leave the last image up, not a black one.
    if (rend.timer.inFlight() >= 2) return;
    if (samples == 0 && band3DRow == 0) rend.clear3D(rend.accum);

    View3D v = makeView(rw, rh);
    float perSample = perSampleMsFull * scale * scale;
    if (!interactive && perSample > targetMs * 2.0f) {
        // A full sample would stall the desktop: render it in bands over several frames.
        // Each pixel's alpha counts its own samples, so a partly finished pass is fine.
        int rows = std::clamp((int)(targetMs / (perSample / rh)), 1, rh - band3DRow);
        int sc[4] = {0, rh - band3DRow - rows, rw, rows};
        rend.timer.begin(scale, (float)rows / rh);
        bool ok = rend.renderSample3D(rend.accum, samples, f, view.rs, v, sc);
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
    int n = recording() ? std::min(session.cli.recordSamples, maxS - samples) : 1;  // (a recording has time to smooth motion)
    if (!interactive) {
        n = std::clamp((int)(targetMs / std::max(perSample, 0.01f)), 1, 16);
        n = std::min(n, maxS - samples);
    }
    rend.timer.begin(scale, (float)n);
    for (int i = 0; i < n; i++)
        if (rend.renderSample3D(rend.accum, samples, f, view.rs, v)) samples++;
    rend.timer.end();
}

void App::updateProbe() {
    Renderer::ProbeResult r;
    // (--fixed-dt test runs aren't paced by a display: waiting here gives them what a real
    // window gets - readings one frame old - so their flights are the same every time)
    while (!probeQueue.empty() && rend.fetchProbe(r, session.cli.fixedDt > 0)) {
        PendingProbe req = std::move(probeQueue.front());
        probeQueue.erase(probeQueue.begin());
        // the camera may have moved since the probe was taken: correct for that, so a
        // late result never claims more free space ahead than there is
        Vec3 moved = view.cam.pos - req.pos;
        if (req.tag == 0) {
            probeValid = true;
            deAtCam = r.deAtCam - moved.length();
            centerHitT = r.hitT > 0 ? std::max(r.hitT - moved.dot(req.dir), 0.0f) : r.hitT;
            if (req.gradH > 0) {  // the gradient from the tetrahedron's corners (see probe.frag)
                Vec3 g = Vec3(1, -1, -1) * r.grad[0] + Vec3(-1, -1, 1) * r.grad[1] + Vec3(-1, 1, -1) * r.grad[2] + Vec3(1, 1, 1) * r.grad[3];
                shipNormalValid = g.length() > 0 && std::isfinite(g.length());
                if (shipNormalValid) shipNormal = g.normalized();
            }
            if (!req.whiskers.empty() && r.whiskers.size() == req.whiskers.size()) {
                ShipSensors s;
                s.valid = std::isfinite(r.deAtCam);
                // (fractals whose estimate runs long are marched with a smaller step factor:
                // the ship takes the same margin in how far it may move - not in how far it
                // thinks the fractal is, which steers it)
                s.de = r.deAtCam * std::clamp(view.rs.stepFactor, 0.05f, 1.0f);
                s.objectDe = r.objectDe;
                s.normal = shipNormal;
                s.normalValid = shipNormalValid;
                s.dirs = req.whiskers;
                s.range = req.range;
                s.eps = std::max(req.pos.length() * 4e-6f, 1e-7f);
                for (float h : r.whiskers) {
                    s.free.push_back(h < 0 ? req.range : h);
                    s.hit.push_back(h >= 0);
                }
                shipSensors = std::move(s);
                shipSensorsAt = req.pos;
            }
        } else if (r.hitT > 0) {
            // turn smoothly toward the clicked point; it becomes the new orbit center
            Camera goal = view.cam;
            goal.lookAt(view.cam.pos, view.cam.pos + req.dir * r.hitT);
            camAnim = {true, (float)now, view.cam.yaw, view.cam.pitch, view.cam.distance, goal.yaw, goal.pitch, goal.distance};
            toast("Turning to face the point you double-clicked - it's the new orbit center", 1.5f);
        } else {
            toast("Nothing there - double-click on the fractal surface", 1.5f);
        }
    }
    View3D v = makeView(fbW, fbH);
    Renderer::ProbeRequest req;
    req.pixelAngle = 2.0f * v.tanHalfFov / std::max(fbH, 1);
    req.dir = v.fwd;
    int tag = 0;
    if (pickRequested) {
        float sx = (float)fbW / std::max(winW, 1), sy = (float)fbH / std::max(winH, 1);
        float ux = (float)(pickX * sx - fbW * 0.5) / fbH, uy = (float)(fbH * 0.5 - pickY * sy) / fbH;
        req.dir = (v.fwd + (v.right * ux + v.up * uy) * (2.0f * v.tanHalfFov)).normalized();
        tag = 1;
    }
    if (tag == 0 && (autopilot.active || cockpit.show)) {
        // what the autopilot steers by: the surface's direction, and whiskers around the heading
        float de = probeValid && deAtCam > 0 ? deAtCam : view.cam.distance * 0.1f;
        // (never finer than floats resolve at the camera's position: the samples would coincide)
        // (wide enough to follow the shape rather than every bump on it)
        req.gradH = std::max({de * 0.6f, v.pos.length() * 4e-6f, 1e-7f});
        if (autopilot.active) {
            req.whiskers = autopilot.whiskerDirs();
            req.whiskerRange = autopilot.whiskerRange(de);
        }
    }
    if (rend.probe(fractal(), view.rs, v, req)) {
        PendingProbe p;
        p.tag = tag;
        p.dir = req.dir;
        p.pos = v.pos;
        p.whiskers = req.whiskers;
        p.range = req.whiskerRange;
        p.gradH = req.gradH;
        probeQueue.push_back(std::move(p));
        if (tag == 1) pickRequested = false;
    }
}

// ------------------------------------------------------------------ the spaceship
// Engages the autopilot from wherever the camera is. The clearance to keep comes from the
// fractal's @autopilot hint (a fraction of its default camera distance).
void App::engageAutopilot(int style) {
    if (view.mode != ViewMode::Fractal3D) {
        toast("The autopilot flies the 3D fractals - press M for 3D mode", 2.5f);
        return;
    }
    const Fractal& f = fractal();
    if (style < 0) style = f.autopilotStyle;
    const bool fresh = !autopilot.active;  // (Shift+G switches the style of a flight under way)
    float clearance = f.autopilotClearance * f.sceneSize();
    auto remembered = userClearance.find(f.key + (style == 1 ? "/through" : "/around"));
    if (remembered != userClearance.end()) {
        clearance = remembered->second * f.sceneSize();  // what you set with the slider last time
    } else if (probeValid && deAtCam > 0) {
        // The ship flies at the scale it finds: if you've zoomed in close (the wheel, WASD,
        // a tour stop), it keeps to that scale instead of climbing away from what you were
        // looking at. The scale is how close the surface is, or how far the point you're
        // looking at is (the orbit target) - whichever is larger, so that starting right
        // against a wall still leaves room to fly.
        clearance = std::min(clearance, std::max(deAtCam * 1.5f, view.cam.distance * 0.1f));
    }
    // Through, where the inside is far finer than the outside ("inside=" in the fractal's
    // @autopilot): the ship grows to the scale above in the open and shrinks to the inside's
    // where it's enclosed (see Autopilot::clearanceOpen)
    float open = 0;
    if (remembered == userClearance.end() && style == 1 && f.autopilotInside > 0) {
        open = clearance;
        clearance = std::min(clearance, f.autopilotInside * f.sceneSize());
    }
    unsigned seed = session.cli.fixedDt > 0 ? 12345u : (unsigned)(now * 1000.0) ^ 0x9e3779b9u;  // (tests: the same flight every time)
    autopilot.engage(style == 1 ? FlightStyle::Through : FlightStyle::Around, makeView(fbW, fbH).fwd, clearance, seed, open);
    autopilot.roll = view.cam.roll;  // (carry on from the current bank, so switching style doesn't jolt the view)
    autopilot.lookIn = f.autopilotLook;
    autopilot.orbitCenter = f.autopilotOrbit > 0;
    autopilot.center = Vec3(f.camTarget[0], f.camTarget[1], f.camTarget[2]);
    autopilot.orbitRadius = f.autopilotOrbit * f.sceneSize();
    shipSensors = ShipSensors();  // (whiskers from before don't match the new heading, nor the normal the new scene)
    shipNormalValid = false;
    // Path tracing is all noise in motion: real-time while flying, and back afterwards. A
    // flight already under way keeps its promise to restore it (Shift+G switches style).
    if (view.rs.renderMode == 1) {
        autopilotRestorePT = true;
        view.rs.renderMode = 0;
    } else if (fresh) {
        autopilotRestorePT = false;
    }
    cockpit.show = true;
    toast(style == 1 ? "Autopilot: exploring the inside - move or press G to take over"
                     : "Autopilot: circling the outside - move or press G to take over",
          2.5f);
}

void App::disengageAutopilot(const char* why) {
    if (!autopilot.active) return;
    autopilot.active = false;
    tourFlight.active = false;
    if (autopilotRestorePT) {
        view.rs.renderMode = 1;
        autopilotRestorePT = false;
    }
    if (why) toast(why, 2.0f);
}

// One frame of autopilot flight: the ship moves and the camera follows its view.
void App::flyAutopilot(float dt) {
    ShipSensors s = shipSensors;
    if (s.valid && s.de > s.eps) {
        // The readings are a frame or two old: take the distance flown since off them. If
        // that uses up the free space they reported, hold still until fresh ones arrive
        // (they would otherwise read as touching, and the ship would back away on stale data).
        float moved = (view.cam.pos - shipSensorsAt).length();
        s.de -= moved;
        s.objectDe -= moved;
        for (auto& f : s.free) f = std::max(f - moved, 0.0f);
        if (s.de <= s.eps) s.valid = false;
    }
    s.normal = shipNormal;
    s.normalValid = shipNormalValid;
    Vec3 look;
    Vec3 pos = autopilot.step(dt, view.cam.pos, s, look);
    if (session.cli.flightReport && shipSensors.valid) {  // measured on fresh readings only
        FlightStats& st = flightStats;
        float keep = std::max(autopilot.wallDistance, 1e-30f);
        if (shipSensors.de <= shipSensors.eps) st.touches++;
        else st.closest = std::min(st.closest, shipSensors.de / keep);
        st.travelled += (pos - view.cam.pos).length() / keep;
        st.seconds += dt;
    }
    // the orbit target (and so the renderer's sense of scale) follows the surface ahead, as when flying with WASD
    // (it also sets how far the renderer draws: with nothing ahead, it eases back out to the
    // fractal's size, or everything beyond the last wall passed would vanish into the fog)
    // The renderer's sense of scale (fog, how far it draws) follows this distance, so it must
    // change slowly: chasing whichever wall is dead ahead made it swing tenfold within half a
    // second in the Kleinian caves, and the fog visibly thickened and thinned with it. It
    // eases in log space over about a second and a half, within sensible bounds.
    bool hit = probeValid && centerHitT > 0;
    float S = fractal().sceneSize();
    float want = std::clamp(hit ? centerHitT : S, std::max(autopilot.wallDistance, autopilot.clearance * 0.1f) * 2.0f, S * 2.0f);
    float dist = std::exp(std::log(std::max(view.cam.distance, 1e-9f)) +
                          (std::log(std::max(want, 1e-9f)) - std::log(std::max(view.cam.distance, 1e-9f))) * (1 - std::exp(-dt / 1.5f)));
    view.cam.lookAt(pos, pos + look * dist);
    view.cam.roll = autopilot.roll;
}

void App::updateCockpit(float dt) {
    if (!autopilot.active && view.cam.roll != 0.0f) {
        // Level the wings once the autopilot lets go - quickly: roll is part of the view, so
        // the image can't start refining until it's level (about a third of a second).
        float step = std::max(std::abs(view.cam.roll) * 6.0f, 1.5f) * dt;
        view.cam.roll = std::abs(view.cam.roll) <= step ? 0.0f : view.cam.roll - std::copysign(step, view.cam.roll);
    }
    if (tourFlight.active && autopilot.active && autopilot.time >= tourFlight.seconds) tourFlightNext();
    if (!cockpit.show || view.mode != ViewMode::Fractal3D) {
        cockpit.lastPosValid = false;
        return;
    }
    if (!probeValid) {  // the view jumped (a load, a reset, another fractal): no speed or trail across it
        cockpit.lastPosValid = false;
        cockpit.trail.clear();
        cockpit.speed = 0;
    }
    Vec3 p = view.cam.pos;
    if (cockpit.lastPosValid && dt > 0) {
        float v = (p - cockpit.lastPos).length() / dt;
        cockpit.speed += (v - cockpit.speed) * (1 - std::exp(-dt * 4.0f));
    }
    cockpit.lastPos = p;
    cockpit.lastPosValid = true;
    // the needles: the clearance eased in log space (bumpy walls make the raw reading jump),
    // the speed dial from that eased clearance, both with about a quarter second's lag
    {
        float de = probeValid && deAtCam > 0 ? deAtCam : 0;
        float k = 1 - std::exp(-dt / 0.25f);
        if (de <= 0) {
            cockpit.shownValid = false;
        } else if (!cockpit.shownValid) {
            cockpit.shownDe = de;
            cockpit.shownRush = cockpit.speed / de;
            cockpit.shownValid = true;
        } else {
            cockpit.shownDe = std::exp(std::log(cockpit.shownDe) + (std::log(de) - std::log(cockpit.shownDe)) * k);
            cockpit.shownRush += (cockpit.speed / cockpit.shownDe - cockpit.shownRush) * (1 - std::exp(-dt / 0.5f));  // (a heavier needle)
        }
    }
    if (now - cockpit.lastTrail > 0.1) {
        cockpit.lastTrail = now;
        if (cockpit.trail.empty() || (cockpit.trail.back() - p).length() > 1e-9f) cockpit.trail.push_back(p);
        if (cockpit.trail.size() > 300) cockpit.trail.erase(cockpit.trail.begin());
    }
    // the map's range follows the clearance, like a GPS zooming in on a winding road
    float de = probeValid && deAtCam > 0 ? deAtCam : view.cam.distance * 0.1f;
    float want = std::clamp(de * 14.0f, fractal().sceneSize() * 1e-6f, fractal().sceneSize() * 2.0f);
    cockpit.span = cockpit.span <= 0 ? want : cockpit.span * std::pow(want / cockpit.span, 1 - std::exp(-dt * 1.5f));
    if (std::abs(want / cockpit.span - 1.0f) < 1e-3f) cockpit.span = want;  // (settles, so a still view stops redrawing)
    // Redraw only when something it shows changed: the view (camera, fractal, parameters:
    // the 3D renderer's signature), the range or the size.
    int size = std::max(64, (int)(ImGui::GetFontSize() * 9.0f));
    std::vector<uint8_t> sig = lastSig3D;
    const uint8_t* extra = reinterpret_cast<const uint8_t*>(&cockpit.span);
    sig.insert(sig.end(), extra, extra + sizeof(float));
    sig.insert(sig.end(), reinterpret_cast<const uint8_t*>(&size), reinterpret_cast<const uint8_t*>(&size) + sizeof(int));
    if (sig == cockpit.mapSig && cockpit.map.tex) return;
    float yaw = view.cam.yaw;
    Vec3 fwd(std::sin(yaw), 0, std::cos(yaw)), right(std::cos(yaw), 0, -std::sin(yaw));
    if (rend.renderMap(fractal(), view.rs, makeView(fbW, fbH), p, right, fwd, cockpit.span, cockpit.map, size)) cockpit.mapSig = std::move(sig);
}

// The autopilot tour: each 3D stop of the guided tour, flown for a while.
void App::tourFlightNext() {
    int n = (int)ui.tourStops.size();
    bool keep = tourFlight.active;  // (loading a 2D stop on the way leaves the cockpit)
    for (int tries = 0; tries < n; tries++) {
        tourStep(1);
        if (view.mode == ViewMode::Fractal3D) {
            autopilot.active = false;
            engageAutopilot(-1);
            tourFlight.active = keep && autopilot.active;
            cockpit.trail.clear();
            return;
        }
    }
    tourFlight.active = false;
    toast("The tour has no 3D stops to fly", 2.5f);
}

// ------------------------------------------------------------------ 2D rendering
// Five pixels of a band (its middle row's ends, quarters and center), set up as the
// kernel's first pass sets up every pixel: at delta 0 (Julia sets: at the pixel), or
// where the series approximation puts them.
void App::setupProbes2D(Job2D& job, int tw, int th, int y0, int rows, int startIter) {
    job.probes.clear();
    double pixel = job.cs.height / std::max(th, 1);
    const double* off = rend.deepOffset();
    double shift[2] = {0, 0};
    const SeriesResult* sa = rend.seriesInUse(job.cs, tw, th, shift);
    int xs[5] = {0, tw / 4, tw / 2, (3 * tw) / 4, tw - 1};
    int ys[5] = {y0 + rows / 2, y0 + rows / 4, y0 + rows / 2, y0 + (3 * rows) / 4, y0 + rows / 2};
    for (int n = 0; n < 5; n++) {
        OrbitProbe p;
        double sx = (xs[n] + 0.5 - 0.5 * tw) * pixel, sy = (ys[n] + 0.5 - 0.5 * th) * pixel;  // as samplePos * uPixelSize
        double dc[2] = {sx + off[0], sy + off[1]};
        if (job.cs.julia) p.eps[0] = dc[0], p.eps[1] = dc[1];
        else p.dc[0] = dc[0], p.dc[1] = dc[1];
        if (sa && startIter > 0) {
            std::complex<double> u((sx + shift[0]) * sa->invR, (sy + shift[1]) * sa->invR), eta(0);
            for (int q = (int)sa->coef.size() / 2 - 1; q >= 0; q--) eta = (eta + std::complex<double>(sa->coef[2 * q], sa->coef[2 * q + 1])) * u;
            p.eps[0] = sa->base[0] + eta.real(), p.eps[1] = sa->base[1] + eta.imag();
            p.m = p.i = startIter;
        }
        job.probes.push_back(p);
    }
}

bool App::stepJob2D(Job2D& job, IndexTarget& target, double budgetMs, int stateSlot) {
    int tw = target.w, th = target.h;
    int maxIter = std::max(job.cs.maxIter, 1);
    // Plan this frame's passes from the measured cost per iteration (timer queries,
    // read back a frame later), so the CPU never waits for the GPU. Each pass aims at
    // a third of the budget; the bounded number of passes in flight keeps the GPU
    // from falling far behind when an estimate is too optimistic.
    PassTimer& pt = rend.passTimer(stateSlot);
    pt.poll();
    job.frames++;
    // Pass length follows real measurements only, changing by at most 2x up (4x down) per
    // measurement, and is capped by the worst case: a band's first pass, when every pixel
    // is still iterating. Late passes are cheap because most pixels are done, and a chunk
    // grown on them would make the next band's first pass run for seconds - which the
    // driver kills, silently wiping the image (a 300000-iteration deep zoom did that).
    // The unit of work is iterations x megapixels, so the estimate carries over between
    // bands and images of different sizes (a reduced preview, the anti-aliased image).
    double bandMpx = std::min(rend.bandRowsFor(tw), std::max(th, 1)) * (double)tw / (1 << 20);
    // No pass may hold the GPU for long (the desktop shares it), whatever the average
    // suggests: bounded by the highest cost seen lately (PassTimer::costBound).
    const double passMs = std::min(budgetMs / 3.0, 20.0), queueMs = std::max(2.0 * budgetMs, 50.0);
    bool deep = rend.classicUsesDeep(job.cs, th) && refUploadedOrbit && refUploadedOrbit->size() >= 6;
    if (pt.fresh > 0 && pt.lastMs > 0) {
        double ideal = pt.lastWork * (budgetMs / 3.0) / pt.lastMs / bandMpx;
        job.chunk = (int)std::clamp(ideal, std::max(job.chunk / 4.0, 16.0), std::max(job.chunk * 2.0, 16.0));  // (deep jobs set chunk as low as 1)
        pt.fresh = 0;
    }
    double cap = pt.costBound() > 0 ? passMs / pt.costBound() / bandMpx : 512.0;
    if (!deep) job.chunk = (int)std::clamp((double)job.chunk, (double)std::min(16, maxIter), std::max(16.0, std::min(cap, (double)maxIter)));
    double spent = 0;
    int passes = 0;
    while (job.active && !pt.full() && pt.queuedMs < queueMs) {
        // A new band starts with a "first" pass that initializes its orbits. Only commit
        // to the band once that pass is really issued: if the budget ran out right at a
        // band boundary, the next frame must still begin it with its first pass (it
        // didn't, and the band stayed black).
        bool first = job.bandRows == 0;
        int bandRows = first ? std::min(rend.bandRowsFor(tw), th - job.row) : job.bandRows;
        // With the series approximation every pixel starts at the skip: count from there,
        // or the band would run (and budget) passes with nothing left to do.
        int itersDone = first ? std::min(rend.seriesSkip(job.cs, tw, th), maxIter) : job.itersDone;
        int y0 = th - job.row - bandRows;  // bands run top-down (GL rows count up)
        int k = std::max(std::min(job.chunk, maxIter - itersDone), 1);
        double work = (double)k * bandRows * tw / (1 << 20);
        int trips = 0;  // the kernel's per-pass trip valve (deep zoom)
        if (deep) {
            // Deep zoom: the cost of an iteration swings a thousandfold along the orbit
            // (where the skip-ahead jumps and where it can't), so a pass sized from the
            // last one could hold the GPU for hundreds of milliseconds and stall the
            // desktop. Instead a few of the band's pixels are replayed on the CPU: the pass
            // covers what the slowest of them gets through in the trips that fit passMs,
            // and its work is counted in trips (whose cost is steady).
            if (first) setupProbes2D(job, tw, th, y0, bandRows, itersDone);
            const std::vector<double>& z = *refUploadedOrbit;
            bool blaOn = job.cs.bla && job.cs.coloring != 3 && job.cs.coloring != 4 && job.cs.coloring != 5 && job.cs.coloring != 6;
            const BlaTable* bla = blaOn ? refUploadedBla.get() : nullptr;
            double bail = job.cs.banded ? std::max(job.cs.bailout, 2.0f) : std::max(job.cs.bailout, 64.0f);
            // The valve: the trips whose cost (per trip and megapixel - steady, unlike the
            // cost of an iteration) fills passMs when every pixel uses them all. A pass's work
            // is counted as if every pixel did, so the measured cost is an upper bound.
            // (0.03 ms per trip x megapixel until a full pass has been measured: the running
            // average is in iteration units and mostly fixed overhead - it planned 120 ms passes)
            double costPer = std::max(std::max(pt.peakMsPerWork, pt.worstMsPerWork), 0.03);
            long budgetTrips = std::clamp((long)(passMs / costPer / bandMpx), 16L, (long)(1 << 30));
            int kmax = maxIter - itersDone, kp = kmax;
            bool bounded = false;
            for (auto& p : job.probes) {
                if (p.done) continue;
                OrbitProbe q = p;
                advanceOrbit(z, bla, rend.blaDcMax(), q, maxIter, maxIter, budgetTrips, bail);
                if (q.done && q.i >= maxIter) continue;  // reached the limit: no bound from it
                kp = std::min(kp, std::max(q.i - p.i, 1));
                bounded = true;
            }
            k = bounded ? kp : std::max(std::min(job.chunk, kmax), 1);  // (no probe left: the last size)
            for (auto& p : job.probes) advanceOrbit(z, bla, rend.blaDcMax(), p, itersDone + k, maxIter, std::numeric_limits<long>::max(), bail);
            job.chunk = k;
            trips = (int)budgetTrips;  // pixels slower than the probes are stopped by the valve and finished below
            work = (double)trips * bandRows * tw / (1 << 20);
        }
        double est = pt.msPerWork * work;
        if (passes > 0 && spent + est > budgetMs) break;
        job.bandRows = bandRows;
        job.itersDone = itersDone;
        const IndexTarget* reuse = job.reusePreview && &target != &rend.index2D ? &rend.index2D : nullptr;
        int slot = pt.head;
        pt.begin((float)work, first);
        bool ok = rend.dispatch2D(target, job.cs, y0, job.bandRows, k, first, stateSlot, reuse, trips, slot, std::min(itersDone + k, maxIter));
        pt.end();
        if (!ok) {
            job.active = false;
            return false;
        }
        passes++;
        spent += est;
        job.estMs += est;
        job.itersDone += k;
        if (job.itersDone >= maxIter && deep) {
            // Pixels the valve stopped are behind the band. The band's state images can't
            // be handed to the next band until they're done, so wait for the pass (short by
            // construction) and give them further passes while any is still behind.
            for (;;) {
                pt.waitFor(slot);
                pt.poll();
                if (!rend.passLagged(slot)) break;
                slot = pt.head;
                pt.begin((float)(trips * bandMpx), false);
                ok = rend.dispatch2D(target, job.cs, y0, job.bandRows, maxIter, false, stateSlot, reuse, trips, slot, maxIter);
                pt.end();
                if (!ok) {
                    job.active = false;
                    return false;
                }
                passes++;
            }
        }
        if (job.itersDone >= maxIter) {  // every orbit in the band has escaped or hit the limit
            job.row += job.bandRows;
            job.bandRows = 0;
            if (job.row >= th) {
                job.active = false;
                return true;
            }
        }
    }
    return false;
}

void App::render2D() {
    if (view.cs.formula == kCustomFormula && !rend.hasCustomFormula()) {
        view.cs.formula = 0;
        toast("No custom formula is compiled - showing the Mandelbrot set instead", 4);
    }
    // only fields that change the iteration buffer need a recompute
    Classic2DSettings c = view.cs;
    std::vector<uint8_t> sig;
    appendFields(sig, c, kCompute2D, [](Classic2DSettings& x, auto&& f) { visitClassic(x, f); });
    appendBytes(sig, formulaGeneration);
    syncCenter();  // at deep zooms the doubles don't change when you pan: sign the exact center
    sig.insert(sig.end(), view.hpRe.begin(), view.hpRe.end());
    sig.push_back(0);
    sig.insert(sig.end(), view.hpIm.begin(), view.hpIm.end());
    appendBytes(sig, fbW);
    appendBytes(sig, fbH);
    appendBytes(sig, generation);
    if (sig != coreSig2D) {
        coreSig2D = sig;
        lastChange = now;
    }
    interactive = now - lastChange < 0.2;
    double budgetMs = 1000.0 / std::max(view.rs.targetFps, 10.0f) * 0.8;  // the 3D renderer's budget
    int ss = interactive && !recording() ? 1 : std::clamp(view.cs.supersample, 1, 4);
    int down = interactive && view.rs.adaptiveRes ? down2D : 1;  // (see down2D)
    const std::vector<uint8_t> sigView = sig;  // the view, before the sampling details
    // what is shown of this very view: its sampling (supersample, or -reduction), 0 if another view
    int shownCode = 0;
    bool sameShown = shownSig2D.size() >= sigView.size() + sizeof(int) && std::equal(sigView.begin(), sigView.end(), shownSig2D.begin());
    if (sameShown) std::memcpy(&shownCode, shownSig2D.data() + sigView.size(), sizeof(int));
    if (!interactive && ss > 1 && sameShown && shownCode < 0) ss = 1;  // a reduced preview is up: full size first, then anti-aliased
    appendBytes(sig, down > 1 ? -down : ss);
    int tw = down > 1 ? (fbW + down - 1) / down : fbW * ss, th = down > 1 ? (fbH + down - 1) / down : fbH * ss;
    refPending = false;
    if (rend.classicUsesDeep(view.cs, th)) {
        if (!ensureReference(tw, th, false)) {  // keep showing the old image until the reference is ready
            refPending = true;
            return;
        }
        appendBytes(sig, refUploaded);
    }

    // (re)start a job when the wanted image differs from what's shown or being rendered -
    // except that a 1x preview of this same view is allowed to finish first: it's a
    // quarter of the work, shows a complete picture sooner, and then supplies the
    // anti-aliased render's center samples
    bool previewOfThisView = job2D.active && job2D.cs.supersample == 1 && ss > 1 && job2D.sig.size() >= sigView.size() &&
                             std::equal(sigView.begin(), sigView.end(), job2D.sig.begin());
    bool wantNew = previewOfThisView ? false : job2D.active ? job2D.sig != sig : shownSig2D != sig;
    if (wantNew) {
        if (job2D.active) adaptDown2D(budgetMs, false);  // a preview overtaken by the next move didn't fit its frame
        job2D.active = true;
        job2D.preview = interactive;
        job2D.estMs = 0;
        job2D.frames = 0;
        job2D.row = 0;
        job2D.bandRows = 0;
        // Keep the pass length the previous job learned (stepJob2D still caps it by the
        // measured worst case every frame). Starting every job at 512 iterations meant
        // that while you pan or zoom - a new job every frame - it never grew, so a
        // preview that fits in one pass per band took several, and more than a frame.
        if (job2D.chunk <= 0 || job2D.cs.maxIter != view.cs.maxIter) job2D.chunk = std::min(std::max(view.cs.maxIter, 1), 512);
        job2D.sig = sig;
        job2D.cs = view.cs;
        job2D.cs.supersample = ss;
        // the finished 1x preview of this very view already holds the center samples
        // (whatever reference orbit it used: every reference gives the same values)
        job2D.reusePreview = ss > 1 && down == 1 && sameShown && shownCode == 1;
        // If index2D holds a complete image for this window (at any sampling), keep
        // showing it: draw over it when the size matches (the reveal), otherwise render
        // offscreen and swap when done. After a resize there's nothing valid to keep.
        bool shownValid = rend.index2D.w > 0 && shownFbW == fbW && shownFbH == fbH;
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
    bool done = stepJob2D(job2D, target, budgetMs);
    for (int guard = 0; recording() && !done && job2D.active && guard < 100000; guard++) {  // a recorded frame is a finished one
        glFinish();
        done = stepJob2D(job2D, target, budgetMs);
    }
    if (done) {
        if (job2D.offscreen) {
            rend.index2D.swap(work2D);
            work2D.release();
        }
        shownSig2D = job2D.sig;
        shownFbW = fbW;
        shownFbH = fbH;
        adaptDown2D(budgetMs, true);
    }
}

// A moving preview should fit in one frame: when the last one didn't, the next is drawn
// at half the size (a quarter of the work); when it had plenty of room, at double.
void App::adaptDown2D(double budgetMs, bool finished) {
    if (!job2D.preview || !view.rs.adaptiveRes) return;
    if (!finished || job2D.frames > 1 || job2D.estMs > budgetMs) down2D = std::min(down2D * 2, 16);
    else if (down2D > 1 && job2D.estMs * 4 < budgetMs * 0.6) down2D /= 2;
}

// ------------------------------------------------------------------ deep zoom
void App::syncCenter() {
    if (view.hpRe.empty() || view.cs.cx != view.hpShadow[0] || view.cs.cy != view.hpShadow[1]) {
        view.hpRe = hp::fromDouble(view.cs.cx);
        view.hpIm = hp::fromDouble(view.cs.cy);
        view.hpShadow[0] = view.cs.cx;
        view.hpShadow[1] = view.cs.cy;
    }
}

void App::moveCenter(double dx, double dy) {
    syncCenter();
    int bits = hp::bitsForPixel(view.cs.height / std::max(fbH, 1));
    if (bits <= 64) {  // shallow: doubles are exact enough
        view.cs.cx += dx;
        view.cs.cy += dy;
        return;  // (syncCenter re-derives the decimal form next time)
    }
    view.hpRe = hp::add(view.hpRe, dx, bits);
    view.hpIm = hp::add(view.hpIm, dy, bits);
    view.cs.cx = view.hpShadow[0] = hp::toDouble(view.hpRe);
    view.cs.cy = view.hpShadow[1] = hp::toDouble(view.hpIm);
}

// Makes sure a reference orbit for the current view is on the GPU. A finished
// reference is reused while it's close enough (panning just shifts it); otherwise
// a new one starts on the worker thread. `wait` blocks (offline renders).
bool App::ensureReference(int targetW, int targetH, bool wait) {
    syncCenter();
    const auto& cs = view.cs;
    double pixel = cs.height / std::max(targetH, 1);
    RefOrbitRequest want;
    want.re = view.hpRe;
    want.im = view.hpIm;
    want.julia = cs.julia;
    want.jre = cs.jx;
    want.jim = cs.jy;
    want.maxIter = cs.maxIter;
    want.bits = hp::bitsForPixel(cs.height / std::max(targetH, fbH * std::clamp(cs.supersample, 1, 4)));  // the finest image this view will need
    want.bailout = cs.banded ? std::max(cs.bailout, 2.0f) : std::max(cs.bailout, 64.0f);  // as the shader uses
    // the skip-ahead table is valid for |delta_c| up to: half the (widest) screen's diagonal
    // plus the 4 screens the reference may be reused for
    want.dcMax = cs.julia ? 0.0 : cs.height * 6.5;
    want.blaEps = kBlaEpsilon;
    // A reference serves a view when its point is within a few screens and its orbit is
    // long and precise enough. Its skip-ahead table only reaches |delta_c| <= dcMax:
    // beyond that the kernel iterates without jumps (uBlaDcMax2). So zooming out keeps
    // drawing with the uploaded reference while a wider one is computed - it used to wait
    // for it, and as every frame of the zoom asked for a wider one still, the image froze
    // until you stopped.
    auto serves = [&](const RefOrbitRequest& r, bool withTable) {
        if (r.re.empty() || r.julia != want.julia || r.jre != want.jre || r.jim != want.jim || r.maxIter < want.maxIter ||
            r.bits < want.bits || r.bailout != want.bailout || r.blaEps != want.blaEps || (withTable && r.dcMax < want.dcMax))
            return false;
        double ox = hp::diffOver(want.re, r.re, pixel, want.bits), oy = hp::diffOver(want.im, r.im, pixel, want.bits);
        return std::abs(ox) < 4.0 * targetH && std::abs(oy) < 4.0 * targetH;
    };
    // A request that will serve the view is left to finish, even if a wider table is
    // wanted by now: a zoom that started before the first reference was done kept
    // restarting it, and nothing was ever drawn until the zoom stopped.
    auto takeFinished = [&] {  // (before asking for another one: a request replaces the finished orbit)
        if (refWorker.ready() && refWorker.version() != refUploaded) {
            rend.setReferenceOrbit(refWorker.orbit());
            rend.setBlaTable(refWorker.bla());
            refUploaded = refWorker.version();
            refUploadedReq = refWorker.current();
            refUploadedOrbit = refWorker.orbitPtr();
            refUploadedBla = refWorker.blaPtr();
        }
    };
    takeFinished();
    if (!serves(refWorker.current(), false) || (refWorker.ready() && !serves(refWorker.current(), true))) refWorker.request(want);
    if (wait) {
        while (!refWorker.ready()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        takeFinished();
    }
    if (!serves(refUploadedReq, false)) return false;  // keep showing the old image until one is ready
    const RefOrbitRequest& ref = refUploadedReq;
    double off[2] = {hp::diffOver(view.hpRe, ref.re, 1.0, ref.bits), hp::diffOver(view.hpIm, ref.im, 1.0, ref.bits)};
    rend.setDeepOffset(off[0], off[1]);
    rend.setBlaDcMax(ref.dcMax);
    // The series that lets every pixel skip the shared start of its orbit: the one made
    // for exactly this view, or else the last one as long as this view lies inside the
    // view it was made for (it's a power series around that view's center, checked at
    // that view's corners, so it's at least as accurate anywhere inside) - which is what
    // keeps it in use while you zoom in.
    const SeriesResult* series = nullptr;
    double shift[2] = {0, 0}, half[2] = {0.5 * targetW * pixel, 0.5 * targetH * pixel};
    if (cs.series && seriesSupported(cs)) {
        SeriesRequest sr;
        sr.orbit = refUploadedOrbit;
        sr.bla = refUploadedBla;
        sr.orbitVersion = refUploaded;
        sr.julia = cs.julia;
        sr.dc0[0] = off[0], sr.dc0[1] = off[1];
        sr.half[0] = half[0], sr.half[1] = half[1];
        sr.maxIter = cs.maxIter;
        sr.bailout = want.bailout;
        auto covers = [&](const SeriesRequest& big) {  // this view lies inside the view `big` was made for
            return big.orbitVersion == refUploaded && big.julia == sr.julia && big.maxIter == sr.maxIter && big.bailout == sr.bailout &&
                   std::abs(off[0] - big.dc0[0]) + half[0] <= big.half[0] && std::abs(off[1] - big.dc0[1]) + half[1] <= big.half[1];
        };
        if (wait) {
            seriesWorker.request(sr);
            seriesWorker.wait();
        }
        if (const SeriesResult* done = seriesWorker.resultFor(seriesWorker.current()))  // the newest finished one
            if (!seriesHeldReq.sameView(seriesWorker.current())) {
                seriesHeld = *done;
                seriesHeldReq = seriesWorker.current();
            }
        // Ask for this view's own series - unless one that covers this view is being
        // computed (zooming in asks for a slightly smaller view every frame, which would
        // restart it forever). While the view moves, ask for one covering 4x the view, so
        // zooming out is covered for a while too.
        if (!seriesHeldReq.sameView(sr) && !(seriesWorker.busy() && covers(seriesWorker.current()))) {
            SeriesRequest ask = sr;
            if (interactive) ask.half[0] *= 4, ask.half[1] *= 4;
            seriesWorker.request(ask);
        }
        if (seriesHeld.skip > 0 && covers(seriesHeldReq)) {
            series = &seriesHeld;
            shift[0] = off[0] - seriesHeldReq.dc0[0], shift[1] = off[1] - seriesHeldReq.dc0[1];
        }
    }
    rend.setSeries(series, shift, half);
    return true;
}

// Colorings that look at every iterate (stripes, traps) and biomorphs need the whole orbit.
bool App::seriesSupported(const Classic2DSettings& cs) const { return cs.coloring < 3; }

// ------------------------------------------------------------------ Julia inset
// Fractint could show the Julia set belonging to the point under the cursor
// while you explored the Mandelbrot set. Same here: a small live preview.
const FormulaDef* App::juliaPartner() const {
    if (view.cs.formula != kCustomFormula) return nullptr;
    const FormulaDef* d = findFormula(view.formulaName);
    return d && !d->julia.empty() ? findFormula(d->julia) : nullptr;
}

void App::updateJuliaInset() {
    const auto& c0 = view.cs;
    const FormulaDef* partner = juliaPartner();
    bool usable = ui.showJuliaInset && view.mode == ViewMode::Classic2D && !c0.julia && c0.formula != 4 &&
                  (c0.formula != kCustomFormula || partner) && ImGui::IsMousePosValid() && !ImGui::GetIO().WantCaptureMouse;
    if (!usable) return;
    bool restart = false;
    if (partner) {  // custom formulas: the inset draws the @julia partner with c = p1 (its own program slot)
        std::string key = partner->source + "|" + std::to_string(view.fn[0]) + std::to_string(view.fn[1]) +
                          std::to_string(view.fn[2]) + std::to_string(view.fn[3]);
        if (key != inset.formula) {
            TranspiledFormula t = transpileFormula(partner->source, view.fn);
            if (!t.ok) return;
            rend.setCustomFormula(t.glsl, 1);
            inset.formula = key;
            restart = true;
        }
    }
    ImGuiIO& io = ImGui::GetIO();
    float sx = (float)fbW / std::max(winW, 1), sy = (float)fbH / std::max(winH, 1);
    double ps = c0.height / fbH;
    double px = c0.cx + (io.MousePos.x * sx - fbW * 0.5) * ps, py = c0.cy + (fbH * 0.5 - io.MousePos.y * sy) * ps;
    if (c0.formula == 1) py = -py;  // Burning Ship is drawn flipped
    int size = std::clamp(fbH / 3, 128, 512);
    if (!inset.index.ensure(size, size) || !inset.image.ensure(size, size, GL_RGBA8, GL_LINEAR)) return;
    if (restart || px != inset.jx || py != inset.jy || (!inset.job.active && !inset.ready)) {
        inset.jx = px;
        inset.jy = py;
        Job2D& j = inset.job;
        j = Job2D();
        j.active = true;
        j.cs = c0;
        j.cs.julia = true;
        j.cs.jx = px;
        j.cs.jy = py;
        j.cs.cx = 0;
        j.cs.cy = 0;
        j.cs.height = 3.2;
        if (partner) {  // the partner's own defaults and view, with c = p1 = the point
            j.cs.julia = false;
            for (int i = 0; i < kFormulaParams; i++)
                j.cs.formulaP[i][0] = partner->hasP[i] ? partner->p[i][0] : 0.0f, j.cs.formulaP[i][1] = partner->hasP[i] ? partner->p[i][1] : 0.0f;
            j.cs.formulaP[0][0] = (float)px, j.cs.formulaP[0][1] = (float)py;
            if (partner->hasView) j.cs.cx = partner->view[0], j.cs.cy = partner->view[1], j.cs.height = partner->view[2];
        }
        j.cs.supersample = 1;
        j.cs.maxIter = std::min(c0.maxIter, 2000);
        j.chunk = j.cs.maxIter;
        inset.ready = false;
    }
    if (inset.job.active && stepJob2D(inset.job, inset.index, 4.0, 1)) {
        RenderSettings r = view.rs;
        r.pixelSize = 1, r.scanlines = 0;
        rend.display(ViewMode::Classic2D, nullptr, &inset.index, r, inset.job.cs, cycleOffset, size, size, inset.image.fbo);
        inset.ready = true;
    }
}

// ------------------------------------------------------------------ screenshots & posters
void App::takeScreenshot() {
    std::error_code ec;
    fs::create_directories(session.picturesDir, ec);
    std::string base = (session.picturesDir / ("fract3d-" + timestampName())).string();
    std::vector<uint8_t> px;
    bool ok = rend.readImage(view.mode, &rend.accum, &rend.index2D, view.rs, view.cs, cycleOffset, fbW, fbH, px) &&
              writePngWithText(base + ".png", fbW, fbH, px.data(), parText());
    toast(ok ? "Saved " + base + ".png - drop it on the window to reopen this view" : "Screenshot failed", 4);
}

void App::startPoster(int w, int h, int nSamples) {
    poster.active = true;
    poster.w = w;
    poster.h = h;
    poster.samples = view.mode == ViewMode::Fractal3D ? std::max(nSamples, 1) : 1;
    poster.done = 0;
    poster.tile = 0;
    poster.started = glfwGetTime();
    poster.view = makeView(w, h);
    poster.rs = view.rs;
    poster.cs = view.cs;
    poster.cs.supersample = std::clamp(view.cs.supersample, 1, 4);
    poster.fractal = fractal();
    for (auto& p : poster.fractal.params) {  // freeze animated parameters at their current value
        paramEffective(p, animTime, p.value);
        p.animate = false;
    }
    poster.palette = palettes[view.rs.palette];
    poster.cycleOffset = cycleOffset;
    poster.par = parText();
    poster.mode = view.mode;
    std::error_code ec;
    fs::create_directories(session.picturesDir, ec);
    poster.path = (session.picturesDir / ("fract3d-" + timestampName() + "-" + std::to_string(w) + "x" + std::to_string(h) + ".png")).string();
    bool ok;
    if (view.mode == ViewMode::Fractal3D) {
        ok = poster.target.ensure(w, h, GL_RGBA32F, GL_LINEAR);
        if (ok) rend.clear3D(poster.target);
    } else {
        if (poster.cs.formula == kCustomFormula && !rend.hasCustomFormula()) poster.cs.formula = view.cs.formula = 0;
        int ss = poster.cs.supersample;
        if (rend.classicUsesDeep(poster.cs, h * ss)) ensureReference(w * ss, h * ss, true);  // offline: just wait for it
        ok = poster.index.ensure(w * ss, h * ss);
        if (ok) {
            poster.index.clear();
            poster.job = Job2D();
            poster.job.active = true;
            poster.job.cs = poster.cs;
            poster.job.chunk = std::min(std::max(view.cs.maxIter, 1), 512);
        }
    }
    if (!ok) {
        char buf[160];
        snprintf(buf, sizeof buf, "Can't render %dx%d: too large for this GPU (max %d per side, including anti-aliasing)",
                 w, h, maxTextureSize());
        toast(buf, 6);
        poster.active = false;
        exitCode = 1;
        if (!session.cli.shotPath.empty()) quit = true;
    }
}

void App::updatePoster() {
    const int T = 1024;
    bool is3D = poster.mode == ViewMode::Fractal3D;
    double budget = session.cli.shotPath.empty() ? 30.0 : 500.0;
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
                if (!session.cli.shotPath.empty()) quit = true;
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
            if (!session.cli.shotPath.empty()) quit = true;
            return;
        }
    }
    if (finished && poster.toVideo) {  // one frame of a video: straight to the encoder
        std::vector<uint8_t> px;
        rend.setPalette(poster.palette);
        bool ok = rend.readImage(poster.mode, &poster.target, &poster.index, poster.rs, poster.cs, poster.cycleOffset,
                                 poster.w, poster.h, px);
        applyPalette();
        poster.active = false;
        poster.toVideo = false;
        if (ok) videoFrameRendered(px);
        else finishVideo(false);
        return;
    }
    if (finished && !is3D && !session.cli.dumpIterations.empty()) {
        // raw iteration values (int32 width, height, then float rows top-down) for tools/itercheck
        int w = poster.index.w, h = poster.index.h;
        std::vector<float> v((size_t)w * h);
        glGetTextureImage(poster.index.value, 0, GL_RED, GL_FLOAT, (GLsizei)(v.size() * sizeof(float)), v.data());
        if (FILE* f = fopen(session.cli.dumpIterations.c_str(), "wb")) {
            int32_t hdr[2] = {w, h};
            fwrite(hdr, sizeof hdr, 1, f);
            for (int y = h - 1; y >= 0; y--) fwrite(&v[(size_t)y * w], sizeof(float), w, f);
            fclose(f);
        }
    }
    if (finished) {
        std::vector<uint8_t> px;
        rend.setPalette(poster.palette);
        bool ok = rend.readImage(poster.mode, &poster.target, &poster.index, poster.rs, poster.cs, poster.cycleOffset,
                                 poster.w, poster.h, px) &&
                  writePngWithText(poster.path, poster.w, poster.h, px.data(), poster.par);  // the view travels inside the PNG
        applyPalette();  // back to whatever the live view uses
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
        if (!session.cli.shotPath.empty()) quit = true;
    }
}

// ------------------------------------------------------------------ 2D <-> 3D bridge
void App::liftTo3D() {
    bool custom = view.cs.formula == kCustomFormula;
    if (!custom && (view.cs.formula > 3 || (view.cs.julia && view.cs.formula != 0))) {
        toast("The 3D landscape supports Mandelbrot (and its Julia sets), Burning Ship, Tricorn, Multibrot z^3 "
              "and custom formulas", 4);
        return;
    }
    int li = lib_.indexOf("landscape");
    if (li < 0) {
        toast("landscape.glsl not found");
        return;
    }
    Fractal& f = lib_.all()[li];
    int formula = 0;
    switch (view.cs.formula) {
    case 1: formula = 1; break;
    case 2: formula = 2; break;
    case 3: formula = 3; break;
    default: formula = 0;
    }
    if (view.cs.julia && view.cs.formula == 0) formula = 4;
    if (custom) formula = 5;
    if (auto* p = f.find("formula")) p->value[0] = (float)formula;
    if (auto* p = f.find("center")) p->setPrecise(0, view.cs.cx), p->setPrecise(1, view.cs.cy);  // every digit (deep lifts)
    if (auto* p = f.find("zoom")) p->value[0] = (float)(3.0 / view.cs.height);
    if (auto* p = f.find("iterations")) p->value[0] = (float)std::clamp(view.cs.maxIter, 10, 1500);
    if (auto* p = f.find("juliaC")) { p->value[0] = (float)view.cs.jx; p->value[1] = (float)view.cs.jy; }
    int palette = view.rs.palette;  // the point of lifting is continuity: keep the 2D palette
    selectFractal(li, true);
    setMode(ViewMode::Fractal3D);
    view.rs.palette = palette;
    applyPalette();
    view.rs.colorMode = 0;
    view.rs.colorScale = view.cs.colorDensity;  // landscape trap.x = iterations / 256, so density maps 1:1
    view.rs.colorOffset = 0;
    view.rs.paletteMix = 1.0f;
    if (view.cs.height < 1e-12) toast("Note: the 3D landscape computes in double precision at most; this deep it gets blocky", 5);
    else toast("Lifted into 3D: height = escape time");
}

void App::flattenTo2D() {
    Fractal& f = fractal();
    const Param *pf = f.find("formula"), *pc = f.find("center"), *pz = f.find("zoom"), *pj = f.find("juliaC"),
                *pi = f.find("iterations");
    if (f.key == "landscape" && pf && pc && pz && pj && pi) {  // (a live-edited landscape.glsl may lack some)
        int formula = (int)pf->value[0];
        view.cs.formula = formula == 4 ? 0 : formula == 5 ? kCustomFormula : formula;
        view.cs.julia = formula == 4;
        if (formula == 3) view.cs.power = 3;
        view.cs.cx = pc->precise(0);
        view.cs.cy = pc->precise(1);
        view.cs.height = 3.0 / std::max(pz->value[0], 1e-6f);
        view.cs.jx = pj->value[0];
        view.cs.jy = pj->value[1];
        view.cs.maxIter = std::max((int)pi->value[0], 1);
        view.cs.colorDensity = std::clamp(view.rs.colorScale, 0.05f, 16.0f);  // same palette spacing as in 3D
    }
    setMode(ViewMode::Classic2D);
}
