#pragma once
#include "camera.h"
#include "fractal_lib.h"
#include "palettes.h"
#include "renderer.h"
#include "state.h"

#include <filesystem>
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
    bool glDebug = false;    // report OpenGL errors (always on in debug builds)
    bool glDebugVerbose = false;
};

class App {
public:
    bool init(const CliOptions& cli);
    int run();
    void shutdown();

    // ---- used by UI (ui.cpp) and PAR I/O (params_io.cpp)
    Fractal& fractal() { return lib_.all()[current_]; }
    void selectFractal(int idx, bool resetView);
    void resetView();
    void applyLook(const Fractal& f);
    void setMode(ViewMode m);
    void applyPalette();
    void takeScreenshot();
    void startPoster(int w, int h, int samples);
    bool savePar(const std::filesystem::path& p);
    std::string parText() const;  // the current view as PAR text
    bool loadPar(const std::filesystem::path& p);
    void liftTo3D();
    void flattenTo2D();
    void toast(const std::string& msg, float seconds = 2.5f);
    void loadPrefs();
    void savePrefs();
    std::vector<std::filesystem::path> listParFiles() const;
    void tourStep(int delta);

private:
    // ---- loop
    void frame();
    void handleKeys();
    void input3D(float dt);
    void input2D(float dt);
    void render3D();
    void render2D();
    void updatePoster();
    void updateProbe();
    View3D makeView(int w, int h) const;
    std::vector<uint8_t> signature3D(int w, int h) const;

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
    bool paramWidget(Param& p);

public:
    // ---- state (public so the UI and PAR code can bind directly)
    GLFWwindow* win = nullptr;
    int fbW = 1280, fbH = 800, winW = 1280, winH = 800;
    std::filesystem::path dataDir, userDir, picturesDir;

    ViewMode mode = ViewMode::Fractal3D;
    FractalLibrary lib_;
    int current_ = 0;
    Camera cam;
    RenderSettings rs;
    Classic2DSettings cs;
    std::vector<Palette> palettes;
    CosinePalette customCosine;
    int customPaletteIdx = -1;  // index of the editable cosine palette
    std::vector<Palette> mapPalettes;  // loaded from palettes/*.map
    float cycleOffset = 0.0f;
    int paletteVersion = 0;
    Renderer rend;

    // accumulation bookkeeping
    std::vector<uint8_t> lastSig3D;
    int samples = 0;
    double lastChange = -10.0;
    float motionScale = 0.6f;
    float perSampleMsFull = 8.0f;  // estimated ms per sample at full resolution
    int lastBatch = 1;
    float lastRenderScale = 1.0f;
    bool interactive = false;
    int generation = 0;  // bumped on shader reload to force re-render

    // Progressive 2D rendering: the iteration buffer is filled in top-down bands
    // under a per-frame time budget, so no single draw can stall the GPU (and it
    // reproduces Fractint's scanline reveal).
    // Each band's orbits are advanced a chunk of iterations per compute pass
    // (see classic2d.comp), so even millions of iterations never block the GPU.
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
    bool stepJob2D(Job2D& job, IndexTarget& target, double budgetMs);
    std::vector<uint8_t> coreSig2D;   // last compute signature (without supersampling)
    std::vector<uint8_t> shownSig2D;  // signature of the complete image in index2D
    int band3DRow = 0;                // rows of the current 3D sample already rendered (banded mode)
    float frameBudgetMs = 12.0f;      // GPU time per frame for progressive work

    // probe results
    float deAtCam = 1.0f, centerHitT = -1.0f;
    bool pickRequested = false;
    double pickX = 0, pickY = 0;
    std::vector<int> probeTags;  // FIFO: 0 = center, 1 = pick
    std::vector<Vec3> probeDirs;

    // input
    int dragButton = -1;
    double lastMouseX = 0, lastMouseY = 0, pressX = 0, pressY = 0;
    double lastClickTime = 0;
    float scrollAccum = 0.0f;
    float flySpeed = 1.5f;
    bool keepLighting = false;  // keep the current lighting when switching fractals
    int formulaGeneration = 0;  // bumped when the custom formula is recompiled
    bool flying = false;
    double savedMandel[3] = {-0.6, 0.0, 3.0};

    // time
    double now = 0, lastFrameTime = 0;
    float dt = 0.016f;
    float animTime = 0.0f;
    bool animPaused = false;
    float fps = 0.0f;

    // UI
    bool showUI = true, showLearn = true, showHelp = false, showDemo = false, showPoster = false;
    int uiTheme = 0;  // 0 modern, 1 Fractint
    float uiScale = 1.0f;
    ImFont* fontUI = nullptr;
    ImFont* fontMono = nullptr;
    ImFont* fontRetro = nullptr;
    std::string toastMsg;
    double toastUntil = 0;
    std::string conceptsText;
    int posterW = 3840, posterH = 2160, posterSamples = 256;
    char parName[128] = "my-view";
    std::string parNote;  // the "; comment" line of the last loaded PAR
    int tourIdx = -1;

    // poster job
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

    CliOptions cli;
    RenderTarget uiShotRT;
    int exitCode = 0;
    int frameCount = 0;
    bool fullscreen = false;
    int savedWin[4] = {0, 0, 1600, 900};
    std::string fullscreenMonitor;  // monitor name chosen in View > Fullscreen on (saved in prefs)
    void toggleFullscreen();
    GLFWmonitor* pickMonitor();
    void saveNamedPar();  // File > Save / Ctrl+S
    struct CamAnim {
        bool active = false;
        float start = 0, yaw0 = 0, pitch0 = 0, dist0 = 1, yaw1 = 0, pitch1 = 0, dist1 = 1;
    } camAnim;
    bool quit = false;
    bool idle = false;  // converged and nothing animating: wait for input instead of redrawing
    bool computeIdle();
};

std::string timestampName();
