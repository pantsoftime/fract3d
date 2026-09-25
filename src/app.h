#pragma once
#include "camera.h"
#include "formula.h"
#include "fractal_lib.h"
#include "palettes.h"
#include "renderer.h"
#include "state.h"

#include <filesystem>
#include <random>
#include <string>
#include <vector>

struct GLFWwindow;
struct GLFWmonitor;
struct ImFont;

struct CliOptions {
    std::string parFile;
    std::string fractal;
    std::string shotPath;  // render to this PNG and exit
    int shotW = 0, shotH = 0;
    int shotSamples = 0;
    int mode2d = 0;
    int pathTrace = -1;
    bool hidden = false;
    std::string uiShotPath;  // render N frames with the UI into a PNG and exit (docs/testing)
    int uiShotFrames = 90;
    int theme = -1;          // override the saved UI theme
    std::string formula;     // start in Classic 2D with this formula from the formula files
    bool glDebug = false;    // report OpenGL errors (always on in debug builds)
    bool glDebugVerbose = false;
    // testing aids for --ui-shot runs (there's no real mouse in a hidden window)
    float fakeMouse[2] = {-1, -1};
    bool insetOn = false, orbitOn = false;
};

// Everything that defines what's on screen. (Fractal parameter values live with
// each fractal in the library, so switching back and forth keeps them.)
struct ViewState {
    ViewMode mode = ViewMode::Fractal3D;
    int fractal = 0;  // index into the fractal library
    Camera cam;
    RenderSettings rs;
    Classic2DSettings cs;
    CosinePalette cosine;  // the editable "Custom (cosine editor)" palette
    // the user formula (Classic 2D "Custom formula", and the landscape's)
    std::string formulaName = "Mandel";
    std::string formulaSource;
    int fn[4] = {0, 0, 0, 0};  // fn1..fn4: indexes into kFormulaFunctions
};

// Where things live, how the app was started, and UI preferences that persist
// across runs (prefs.ini) but aren't part of any particular view.
struct Session {
    std::filesystem::path dataDir, userDir, picturesDir;
    std::string imguiIni;
    CliOptions cli;
    int uiTheme = 0;  // 0 modern, 1 Fractint
    float uiScale = 1.0f;
    bool showLearn = true;
    float flySpeed = 1.5f;
    bool keepLighting = false;      // keep the current lighting when switching fractals
    std::string fullscreenMonitor;  // monitor chosen in View > Fullscreen on
};

// Transient overlay state.
struct UiState {
    bool showUI = true, showHelp = false, showDemo = false, showPoster = false;
    bool showJuliaInset = false;
    ImFont* fontUI = nullptr;
    ImFont* fontMono = nullptr;
    ImFont* fontRetro = nullptr;
    std::string toastMsg;
    double toastUntil = 0;
    int posterW = 3840, posterH = 2160, posterSamples = 256;
    char parName[128] = "my-view";
    std::string parNote;  // the "; comment" line of the last loaded PAR (shown on the Tour tab)
    int tourIdx = -1;
    std::mt19937 rng{std::random_device{}()};  // "Surprise me"
    // Learn-panel content, reloaded when the files change (see refreshDocs)
    std::string conceptsText, classicText;
    std::vector<std::pair<std::filesystem::path, std::string>> tourStops;  // preset + its description
    std::filesystem::file_time_type docsTime{}, presetsTime{}, formulasTime{};
    bool showFormulaEditor = false;
    std::string formulaEdit;  // text in the editor (compiled on request)
};

class App {
public:
    bool init(const CliOptions& cli);
    int run();
    void shutdown();

    // ---- used by UI (ui.cpp) and PAR I/O (params_io.cpp)
    Fractal& fractal() { return lib_.all()[view.fractal]; }
    void selectFractal(int idx, bool resetView);
    void resetView();
    void applyLook(const Fractal& f);
    void setMode(ViewMode m);
    void applyPalette();
    void takeScreenshot();
    void startPoster(int w, int h, int samples);
    bool savePar(const std::filesystem::path& p);
    void saveNamedPar();          // File > Save PAR / Ctrl+S
    std::string parText() const;  // the current view as PAR text
    bool loadPar(const std::filesystem::path& p);
    void liftTo3D();
    void flattenTo2D();
    void toast(const std::string& msg, float seconds = 2.5f);
    void loadPrefs();
    void savePrefs();
    std::vector<std::filesystem::path> listParFiles() const;
    void tourStep(int delta);
    void toggleFullscreen();
    bool compileFormula();  // transpiles + compiles view.formulaSource; errors go to formulaError
    bool selectFormula(const std::string& name);
    const FormulaDef* findFormula(const std::string& name) const;

private:
    // ---- loop
    void frame();
    bool computeIdle();
    void handleKeys();
    void input3D(float dt);
    void input2D(float dt);
    void render3D();
    void render2D();
    void updatePoster();
    void updateProbe();
    void refreshDocs();  // (re)loads Learn-panel docs and the tour list when their files change
    View3D makeView(int w, int h) const;
    std::vector<uint8_t> signature3D(int w, int h) const;
    GLFWmonitor* pickMonitor();

    // ---- UI (ui.cpp)
    void setupImGui();
    void applyTheme();
    void drawUI();
    void drawMenuBar();
    void drawControlPanel();
    void drawFractalTab();
    void drawColorTab();
    void drawLightTab();
    void drawRenderTab();
    void drawCameraTab();
    void drawPostTab();
    void drawClassicPanel();
    void drawLearnPanel();
    void drawHud();
    void drawHelp();
    void drawPosterDialog();
    void drawOrbitOverlay();
    void drawToast();
    void drawLesson(const std::string& text);
    void drawFormulaControls();
    void drawJuliaInset();
    void drawFormulaEditor();
    bool paramWidget(Param& p);

public:
    // ---- state (public so the UI and PAR code can bind directly)
    ViewState view;
    Session session;
    UiState ui;

    GLFWwindow* win = nullptr;
    int fbW = 1280, fbH = 800, winW = 1280, winH = 800;
    FractalLibrary lib_;
    std::vector<Palette> palettes;
    int customPaletteIdx = -1;  // index of the editable cosine palette
    float cycleOffset = 0.0f;
    float lastCycleSpeed = 24.0f;  // what C turns cycling back on to
    int paletteVersion = 0;
    Renderer rend;
    std::vector<FormulaDef> formulas;  // formulas/*.frm (built-in, then the user's)
    std::string formulaError;
    TranspiledFormula formulaInfo;     // what the current formula uses (fn1..4, p1..3)

    // progressive 3D accumulation
    std::vector<uint8_t> lastSig3D;
    int samples = 0;
    double lastChange = -10.0;
    float motionScale = 0.6f;
    float perSampleMsFull = 8.0f;  // estimated ms per sample at full resolution
    bool interactive = false;
    int generation = 0;         // bumped on shader reload to force re-render
    int formulaGeneration = 0;  // bumped when the custom formula is recompiled
    int band3DRow = 0;          // rows of the current 3D sample already rendered (banded mode)
    float frameBudgetMs = 12.0f;  // GPU time per frame for progressive work

    // Progressive 2D rendering: the iteration buffer is filled in top-down bands,
    // and each band's orbits are advanced a chunk of iterations per compute pass
    // (see classic2d.comp), so even millions of iterations never block the GPU.
    // It also reproduces Fractint's scanline reveal.
    struct Job2D {
        bool active = false;
        int row = 0;             // rows finished, counted from the top of the target
        int bandRows = 0;        // rows in the band being iterated (0 = start a new band)
        int itersDone = 0;       // iterations run so far on the current band
        int chunk = 4096;        // iterations per pass, adapted to the time budget
        bool offscreen = false;  // rendering into work2D, swapped in when complete
        Classic2DSettings cs;    // settings snapshot (supersample = the job's factor)
        std::vector<uint8_t> sig;
    } job2D;
    IndexTarget work2D;
    // Advances a 2D job on `target` for up to budgetMs; returns true once the image is complete.
    bool stepJob2D(Job2D& job, IndexTarget& target, double budgetMs, int stateSlot = 0);
    std::vector<uint8_t> coreSig2D;   // last compute signature (without supersampling)
    std::vector<uint8_t> shownSig2D;  // signature of the complete image in index2D

    // Julia preview inset (J): the Julia set for the point under the cursor
    struct JuliaInset {
        Job2D job;
        IndexTarget index;
        RenderTarget image;   // palette-mapped result, drawn by ImGui
        double jx = 1e9, jy = 1e9;  // c of the image being shown/rendered
        bool ready = false;
    } inset;
    void updateJuliaInset();

    // probe results
    float deAtCam = 1.0f, centerHitT = -1.0f;
    bool pickRequested = false;
    double pickX = 0, pickY = 0;
    std::vector<int> probeTags;  // FIFO: 0 = center, 1 = pick
    std::vector<Vec3> probeDirs;

    // input
    int dragButton = -1;
    double pressX = 0, pressY = 0;
    bool flying = false;
    double savedMandel[3] = {-0.6, 0.0, 3.0};  // view to return to from a Julia set
    struct CamAnim {
        bool active = false;
        float start = 0, yaw0 = 0, pitch0 = 0, dist0 = 1, yaw1 = 0, pitch1 = 0, dist1 = 1;
    } camAnim;

    // time
    double now = 0, lastFrameTime = 0, lastReloadCheck = 0;
    float dt = 0.016f;
    float animTime = 0.0f;
    bool animPaused = false;
    float fps = 0.0f;

    // high-res render ("poster") job
    struct Poster {
        bool active = false;
        int w = 0, h = 0, samples = 0, done = 0, tile = 0;
        RenderTarget target;
        std::string path;
        double started = 0;
        // Snapshot of everything the image depends on, taken when the render starts,
        // so changing the view, lighting, parameters or palette mid-render can't mix
        // two scenes into one image.
        View3D view;
        RenderSettings rs;
        Classic2DSettings cs;
        Fractal fractal;
        Palette palette;
        float cycleOffset = 0.0f;
        std::string par;       // PAR text describing the snapshot
        ViewMode mode = ViewMode::Fractal3D;
        IndexTarget index;     // 2D posters
        Job2D job;
    } poster;

    RenderTarget uiShotRT;
    int exitCode = 0;
    int frameCount = 0;
    bool fullscreen = false;
    int savedWin[4] = {0, 0, 1600, 900};
    bool quit = false;
    bool idle = false;  // converged and nothing animating: wait for input instead of redrawing
};

std::string timestampName();
