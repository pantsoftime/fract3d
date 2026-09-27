#pragma once
#include "autopilot.h"
#include "camera.h"
#include "deepzoom.h"
#include "formula.h"
#include "fractint_par.h"
#include "fractal_lib.h"
#include "palettes.h"
#include "renderer.h"
#include "state.h"

#include <array>
#include <map>
#include <filesystem>
#include <random>

#include <sys/types.h>
#include <string>
#include <vector>

struct GLFWwindow;
struct GLFWmonitor;
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>  // GLFWgamepadstate
struct ImFont;

struct CliOptions {
    std::string parFile;
    std::string parEntry;  // --par-entry: which entry of a Fractint .PAR file (default: the first)
    std::string fractal;
    std::string shotPath;  // render to this PNG and exit
    int shotW = 0, shotH = 0;
    int shotSamples = 0;
    int mode2d = 0;
    int pathTrace = -1;
    bool hidden = false;
    std::string uiShotPath;  // render N frames with the UI into a PNG and exit (docs/testing)
    int uiShotFrames = 90;
    std::string uiRecord;    // --ui-record OUT.mp4: record frames (UI included) into a video, then exit (trailers, docs)
    int recordFrom = 0;      // --record-from N: the first frame to record (the ones before let the view settle)
    int recordSamples = 4;   // --record-samples N: 3D samples per frame while moving, when recording (anti-aliasing)
    float mouseTo[2] = {-1, -1};  // --mouse-to X,Y: the fake mouse glides from --mouse to here over the frames
    bool hidePanels = false;      // --hide-panels: start with the panels and menu hidden (Shift+Tab)
    float autopilotSpeed = 0;     // --autopilot-speed X: its cruise speed (clearances per second)
    int theme = -1;          // override the saved UI theme
    std::string formula;     // start in Classic 2D with this formula from the formula files
    std::vector<std::string> frmFiles;  // --frm: more formula files to load (like dropping them on the window)
    bool glDebug = false;    // report OpenGL errors (always on in debug builds)
    bool glDebugVerbose = false;
    bool selfTest = false;
    // testing aids for --ui-shot runs (there's no real mouse in a hidden window)
    float fakeMouse[2] = {-1, -1};
    bool insetOn = false, orbitOn = false, hideUi = false;
    std::vector<std::string> openWindows;  // --open gradient|formula|help|render|path
    std::string pathFile;                  // camera path to load (--path); with --render x.mp4 it's exported
    float videoFps = 30;
    float wheel = 0;  // testing: mouse-wheel notches to apply at startup (--wheel)
    int autopilot = -1;  // --autopilot around|through|auto|tour: engage it at startup (2: the fractal's own style, 3: the tour)
    bool cockpit = false;  // --cockpit: start with the spaceship dashboard open
    float fixedDt = 0;     // testing: --fixed-dt S makes every frame advance motion by S seconds
    bool flightReport = false;  // testing: --flight-report prints how the autopilot flew, at exit
    float pathTime = -1;
    std::string dumpIterations;
    int cancelAfterFrames = -1;  // testing: cancel a video export after N frames, like the Cancel button  // --dump-iterations: 2D renders also write the raw iteration buffer (tests)  // --path-time: render the camera path at this time (with --render x.png)
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
    std::vector<GradientStop> gradient = {{0.0f, {0.02f, 0.03f, 0.18f}}, {0.3f, {0.2f, 0.5f, 0.9f}},
                                          {0.55f, {1.0f, 0.95f, 0.8f}}, {0.8f, {0.9f, 0.45f, 0.1f}}};
    // the user formula (Classic 2D "Custom formula", and the landscape's)
    std::string formulaName = "Mandel";
    std::string formulaSource;
    int fn[4] = {0, 0, 0, 0};  // fn1..fn4: indexes into kFormulaFunctions
    // The 2D center in full precision (decimal) for deep zooms. cs.cx/cy hold the
    // nearest doubles; when those are changed directly, this is re-derived.
    std::string hpRe, hpIm;
    double hpShadow[2] = {1e300, 1e300};  // the cs.cx/cy values hpRe/hpIm correspond to
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
    bool restoreSession = true;     // reopen the last view at startup
    std::string fullscreenMonitor;  // monitor chosen in View > Fullscreen on
};

// Transient overlay state.
struct UiState {
    bool showUI = true, showHelp = false, showDemo = false, showPoster = false;
    bool showPanels = true;  // Shift+Tab: the panels and the menu bar (the cockpit and HUD stay)
    bool showJuliaInset = false;
    ImFont* fontUI = nullptr;
    ImFont* fontMono = nullptr;
    ImFont* fontRetro = nullptr;
    std::string toastMsg;
    double toastUntil = 0;
    float lastDisplayW = 0, lastDisplayH = 0;  // the room the panels were last laid out in: width, usable bottom (see fitPanel)
    float panelPrefW[3] = {0, 0, 0}, panelPrefH[3] = {0, 0, 0};  // a panel's size before fitPanel shrank it (0: not shrunk)
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
    bool showGradientEditor = false;
    bool showPathWindow = false;
    char pathName[96] = "my-path";
    int gradientSel = 0;  // selected stop in the gradient editor
    std::string formulaEdit;  // text in the editor (compiled on request)
    const Param* draggingParam = nullptr;  // an animated parameter's slider being dragged (shows its base value)
    std::string centerEdit[2], centerShown[2];  // the Exact center fields, and the center they were filled from
    // importing a Fractint .PAR file with several entries
    bool showFractintImport = false;
    std::vector<FractintEntry> fractintEntries;
    std::string fractintFile, fractintLast;
    std::vector<std::string> fractintWarnings;
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
    bool loadParText(const std::string& text, const std::string& label, bool quiet);
    bool importFractint(const FractintEntry& e);  // one entry of a Fractint .PAR file
    // ---- undo / history (each entry is PAR text: a complete, tested snapshot of the view)
    struct History {
        std::vector<std::pair<std::string, std::string>> entries;  // (label, PAR text)
        int pos = -1;           // entry currently shown
        std::string lastText;   // view as last recorded (or restored)
        double changedAt = -1;  // when the view last differed from lastText
    } history;
    void recordHistory();       // call regularly: records the view once it settles
    void recordHistoryNow();    // records the current view immediately
    int selfTest();             // --self-test: checks app logic that needs no user input
    void undo();
    void redo();
    void jumpToHistory(int i);
    std::string historyLabel() const;
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
    double computeIdle();
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
    void fitPanel(int panel, bool anchoredRight);  // keeps the current panel inside the window when the window is resized
    void drawHud();
    void drawHelp();
    void drawPosterDialog();
    void drawOrbitOverlay();
    void drawToast();
    void drawLesson(const std::string& text);
    void drawFormulaControls();
    void drawJuliaInset();
    void drawGradientEditor();
    void drawPathWindow();
    void drawFormulaEditor();
    void drawFractintImport();
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
    int customPaletteIdx = -1;    // index of the editable cosine palette
    int gradientPaletteIdx = -1;  // index of the editable gradient palette (always last)
    float cycleOffset = 0.0f;
    float lastCycleSpeed = 24.0f;  // what C turns cycling back on to
    int paletteVersion = 0;
    Renderer rend;
    std::vector<FormulaDef> formulas;  // formulas/*.frm (built-in, then the user's)
    std::string formulaError;
    std::string compiledSource;  // what the renderer has now (compileFormula skips identical requests)
    int compiledFn[4] = {-1, -1, -1, -1};
    TranspiledFormula formulaInfo;     // what the current formula uses (fn1..4, p1..5)

    // progressive 3D accumulation
    std::vector<uint8_t> lastSig3D;
    int samples = 0;
    double lastChange = -10.0;
    float motionScale = 0.6f;
    float perSampleMsFull = 8.0f;  // estimated ms per sample at full resolution
    int kind3DFractal = -1, kind3DMode = -1;  // what perSampleMsFull was measured on
    bool interactive = false;
    int generation = 0;         // bumped on shader reload to force re-render
    int formulaGeneration = 0;  // bumped when the custom formula is recompiled
    int band3DRow = 0;          // rows of the current 3D sample already rendered (banded mode)

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
        bool reusePreview = false;  // the shown 1x image of this view supplies the center samples
        bool preview = false;       // started while the view was moving (adapts down2D)
        double estMs = 0;           // estimated GPU time issued so far
        int frames = 0;             // frames it has been stepped
        std::vector<OrbitProbe> probes;  // deep zoom: a few of the band's pixels replayed on the CPU to size passes
        std::vector<uint8_t> sig;
    } job2D;
    // While the view moves, previews are drawn at 1/down2D (up to 1/16) of the window size when a
    // full-size one doesn't fit in a frame (adapted from the measured cost, like the 3D
    // renderer's adaptive resolution); at rest the image goes full size, then anti-aliased.
    int down2D = 1;
    int shownFbW = 0, shownFbH = 0;  // the window size the complete image in index2D was made for
    void adaptDown2D(double budgetMs, bool finished);
    IndexTarget work2D;
    // Advances a 2D job on `target` for up to budgetMs; returns true once the image is complete.
    bool stepJob2D(Job2D& job, IndexTarget& target, double budgetMs, int stateSlot = 0);
    void setupProbes2D(Job2D& job, int tw, int th, int y0, int rows, int startIter);
    std::vector<uint8_t> coreSig2D;   // last compute signature (without supersampling)
    std::vector<uint8_t> shownSig2D;  // signature of the complete image in index2D

    // Julia preview inset (J): the Julia set for the point under the cursor
    struct JuliaInset {
        Job2D job;
        IndexTarget index;
        RenderTarget image;   // palette-mapped result, drawn by ImGui
        double jx = 1e9, jy = 1e9;  // c of the image being shown/rendered
        bool ready = false;
        std::string formula;  // custom formulas: the @julia partner (+ fn choices) compiled for the inset
    } inset;
    const FormulaDef* juliaPartner() const;  // the current custom formula's @julia partner, if any
    void updateJuliaInset();

    // probe results
    float deAtCam = 1.0f, centerHitT = -1.0f;
    bool pickRequested = false;
    double pickX = 0, pickY = 0;
    struct PendingProbe {
        int tag = 0;                 // 0 = center, 1 = pick
        Vec3 dir, pos;               // the ray, and the camera position when the probe was taken (results arrive frames later)
        std::vector<Vec3> whiskers;  // the autopilot's rays, if any
        float range = 0, gradH = 0;
    };
    std::vector<PendingProbe> probeQueue;  // FIFO, in issue order
    bool probeValid = false;     // deAtCam/centerHitT describe the current scene (false after a jump)

    // ---- the spaceship: cockpit dashboard and autopilot (autopilot.h)
    Autopilot autopilot;
    ShipSensors shipSensors;     // the newest whisker readings, taken at shipSensorsAt
    Vec3 shipSensorsAt;
    Vec3 shipNormal{0, 1, 0};    // away from the nearest surface (newest gradient probe)
    bool shipNormalValid = false;
    bool autopilotRestorePT = false;  // it switched path tracing off while flying: back on when it stops
    struct Cockpit {
        bool show = false;
        RenderTarget map;            // the moving map (a slice through the fractal at the ship's height)
        float span = 0;              // across the map (world units), eased toward the wanted range
        std::vector<Vec3> trail;     // where the ship has been (for the map), newest last
        double lastTrail = 0;
        float speed = 0;             // measured, world units per second
        Vec3 lastPos;
        bool lastPosValid = false;
        std::vector<uint8_t> mapSig;  // what the map was last drawn for (see updateCockpit)
    } cockpit;
    struct TourFlight {              // the autopilot tour: flies each 3D stop for a while
        bool active = false;
        float seconds = 45;          // of flight (Autopilot::time) per stop
    } tourFlight;
    void engageAutopilot(int style);   // 0 around, 1 through, -1 the fractal's own style
    std::map<std::string, float> userClearance;  // "fractal/style" -> the Clearance slider's value / scene size (this session)
    void disengageAutopilot(const char* why);
    void flyAutopilot(float dt);
    void updateCockpit(float dt);
    void tourFlightNext();
    struct FlightStats {             // for --flight-report
        float closest = 1e30f;       // nearest surface seen, as a fraction of the distance it meant to keep
        float travelled = 0;         // in those distances
        float seconds = 0;
        int touches = 0;             // frames that started touching a surface
    } flightStats;
    void drawCockpit();
    float cockpitHeight() const;  // screen space it takes along the bottom (0 when hidden)

    // input
    int dragButton = -1;
    double pressX = 0, pressY = 0;
    bool flying = false;
    bool mouseCaptured = false;
    // Mouse-wheel zoom, eased: notches accumulate here and are applied over a few frames
    float wheelPending = 0;
    float takeWheel(float dt);  // this frame's share of the pending notches
    void zoom3D(float notches);  // toward the target, never through the surface ahead
    double captureLast[2] = {0, 0};
    void setMouseCapture(bool on);
    void toggleJulia(double px, double py);  // 2D: Julia set of the point, or back to the parameter plane
    void inputGamepad(float dt);
    bool gamepadActive = false;
    GLFWgamepadstate prevPad{};
    double savedMandel[3] = {-0.6, 0.0, 3.0};  // view to return to from a Julia set
    std::string savedMandelHP[2];
    // custom formulas: the formula and parameters to return to from its @julia partner
    struct {
        std::string formula;
        float p[kFormulaParams][2];
        int fn[4];
    } juliaReturn;
    // deep zoom
    RefOrbitWorker refWorker;
    int refUploaded = -1;
    RefOrbitRequest refUploadedReq;  // the reference the GPU holds (the worker may be on a newer one)
    std::shared_ptr<const std::vector<double>> refUploadedOrbit;
    std::shared_ptr<const BlaTable> refUploadedBla;
    SeriesResult seriesHeld;         // the last series computed: also serves views inside the one it was made for
    SeriesRequest seriesHeldReq;
    bool refPending = false;
    double busySince2D = -1;  // when the 2D image last became unfinished (-1: it's complete); for the panel's status bar
    void syncCenter();                        // make hpRe/hpIm agree with cs.cx/cy
    void moveCenter(double dx, double dy);    // pan by a (tiny) amount without losing precision
    bool ensureReference(int targetW, int targetH, bool wait);
    SeriesWorker seriesWorker;
    bool seriesSupported(const Classic2DSettings& cs) const;
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
        bool toVideo = false;  // a video frame: hand the pixels to the encoder instead of saving a PNG
    } poster;

    // ---- camera paths and video export (animation.cpp)
    struct Keyframe {
        std::string par;        // the complete view
        float duration = 3.0f;  // seconds to the next keyframe
        bool ease = false;      // slow in and out of the segment to the next keyframe (else constant pace)
        std::string label;
        ViewState v;            // parsed from par (see parseKeyframes)
        std::string fractalKey;
        std::vector<std::array<float, 4>> params;
    };
    struct CameraPath {
        std::vector<Keyframe> keys;
        bool parsed = false;
        bool playing = false, loop = false;
        double playStart = 0;
        float time = 0;
        float fps = 30;
        int videoW = 1920, videoH = 1080, videoSamples = 32;
        int encoder = 0;  // 0 libx264, 1 NVIDIA NVENC, 2 PNG images
        // Parameter animation is off while a path drives the view; the flags are
        // saved here the first time the path moves the view and restored afterwards.
        std::vector<std::pair<std::string, std::vector<char>>> savedAnim;
    } camPath;
    void endPathPreview();  // playback/scrubbing ended: parameter animation comes back
    struct VideoJob {
        bool active = false;
        bool finishing = false;  // all frames written (or cancelled): waiting for ffmpeg to exit
        bool ok = false, cancelled = false;
        int fd = -1;
        pid_t pid = 0;
        int frame = 0, frames = 0;
        std::string out, restorePar;
        double started = 0;
        void (*oldSigpipe)(int) = nullptr;
        bool sequence = false;  // PNG images ("frame-%05d.png") instead of an ffmpeg video
    } video;
    void addKeyframe();
    void parseKeyframes();
    float pathDuration() const;
    void applyPathTime(float t);
    void updatePathPlayback();
    bool savePath(const std::filesystem::path& p);
    bool loadPath(const std::filesystem::path& p);
    bool startVideo(const std::string& out);
    void nextVideoFrame();
    void videoFrameRendered(const std::vector<uint8_t>& rgba);
    void finishVideo(bool ok);
    void cancelVideo();
    void pollVideoEncoder(bool wait = false);  // reports the result once ffmpeg has exited

    RenderTarget uiShotRT;
    int exitCode = 0;
    int frameCount = 0;
    FILE* recordPipe = nullptr;  // --ui-record: ffmpeg's stdin
    bool recording() const { return !session.cli.uiRecord.empty(); }
    bool fullscreen = false;
    int savedWin[4] = {0, 0, 1600, 900};
    bool quit = false;
    double idleWait = 0;  // > 0: nothing to draw, so wait this long for input instead of redrawing
    std::vector<std::filesystem::path> droppedFiles;  // from the window's drop callback
    void openDroppedFile(const std::filesystem::path& p);
    double lastSessionSave = 0;
    std::string lastSessionText;
    void saveSession(bool force);
};

std::string timestampName();
bool isImageSequence(const std::string& path);  // "frames/f-%05d.png": a video as numbered PNGs
