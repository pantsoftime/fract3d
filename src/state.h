#pragma once
// All user-facing settings. settings.h lists every field (with flags saying
// what each one affects), and everything else is derived from that table.

enum class ViewMode : int { Fractal3D = 0, Classic2D = 1 };

struct RenderSettings {
    // quality
    int renderMode = 0;  // 0 real-time, 1 path traced
    int bounces = 3;
    int maxSteps = 300;
    float detail = 0.5f;      // surface threshold in pixels
    float stepFactor = 0.9f;  // DE multiplier
    float maxDist = 6.0f;     // x scene scale
    int maxSamplesRT = 48;
    int maxSamplesPT = 4096;

    // lighting
    float sunAzimuth = 35.0f, sunElevation = 38.0f;  // degrees
    float sunColor[3] = {1.0f, 0.9f, 0.78f};
    float sunIntensity = 3.0f;
    float sunSize = 2.0f;  // degrees (angular radius): shadow softness
    bool shadows = true;
    float skyZenith[3] = {0.12f, 0.22f, 0.48f};
    float skyHorizon[3] = {0.66f, 0.70f, 0.80f};
    float skyIntensity = 0.8f;
    int background = 0;  // 0 sky, 1 solid
    float bgColor[3] = {0.015f, 0.015f, 0.025f};
    float aoStrength = 0.8f;
    float fogDensity = 0.0f;
    float fogColor[3] = {0.62f, 0.70f, 0.80f};
    float glowStrength = 0.0f;
    float glowColor[3] = {0.35f, 0.55f, 1.0f};
    bool floorOn = false;
    float floorY = -1.0f;
    float floorColor[3] = {0.26f, 0.26f, 0.28f};

    // material / coloring
    int palette = 0;
    int colorMode = 0;  // 0 trap A, 1 trap B, 2 iterations, 3 normal, 4 flat
    float colorScale = 1.0f, colorOffset = 0.0f;
    float paletteMix = 0.85f;
    float baseColor[3] = {0.80f, 0.78f, 0.74f};
    float specular = 0.25f, roughness = 0.35f;
    float cycleSpeed = 0.0f;  // palette entries per second (0 = off)

    // camera optics
    float fov = 55.0f;
    float aperture = 0.0f;
    bool autoFocus = true;
    float focusDist = 3.0f;

    // post
    float exposure = 1.0f;
    int tonemap = 0;  // ACES, Reinhard, none
    float vignette = 0.25f;
    float saturation = 1.0f;
    int retro = 0;  // off, VGA 256, EGA 16
    int pixelSize = 1;
    float scanlines = 0.0f;

    // performance
    bool adaptiveRes = true;
    float targetFps = 60.0f;
    float stillScale = 1.0f;  // render resolution when the view is still
};

struct Classic2DSettings {
    int formula = 0;  // see kClassicFormulas
    bool julia = false;
    double cx = -0.6, cy = 0.0;  // view center
    double height = 3.0;         // visible height of the complex plane
    double jx = -0.8, jy = 0.156;
    int maxIter = 256;
    float bailout = 2.0f;
    int power = 3;
    float phoenixP[2] = {-0.5f, 0.0f};
    int supersample = 2;
    int fp64 = 2;  // 0 off, 1 on, 2 auto, 3 perturbation
    bool bla = true;  // deep zoom: skip ahead with linear approximations (much faster)
    bool periodicity = true;  // stop early when an orbit is caught in a cycle (Fractint's periodicity checking)
    bool series = true;       // deep zoom: skip the stretch every pixel shares (series approximation)
    bool banded = true;
    float colorDensity = 1.0f;  // palette entries per iteration
    int insideMode = 0;         // black, zmag, solid
    float insideColor[3] = {0.0f, 0.0f, 0.0f};
    float rootSpread = 85.0f;
    bool showOrbit = false;
    int coloring = 0;       // outside coloring mode, see kColoringModes
    float trapSize = 4.0f;  // stripe density / orbit trap scale
    float formulaP[5][2] = {};  // user formula parameters p1..p5 (kFormulaParams)
};

inline const char* kClassicFormulas[] = {"Mandelbrot", "Burning Ship", "Tricorn (Mandelbar)", "Multibrot",
                                         "Newton (z^3 - 1)", "Phoenix", "Lambda", "Magnet I",
                                         "Custom formula"};
inline constexpr int kClassicFormulaCount = 9;
inline constexpr int kCustomFormula = 8;
inline const char* kColoringModes[] = {"Escape time",     "Binary decomposition", "Escape angle", "Stripe average",
                                       "Orbit trap: cross", "Orbit trap: point",   "Biomorph"};
inline constexpr int kColoringModeCount = 7;
inline constexpr int kMaxIterations = 1 << 22;  // progressive rendering keeps even this safe
inline constexpr double kMinHeight = 1e-290;     // deepest 2D zoom (perturbation keeps it sharp)
