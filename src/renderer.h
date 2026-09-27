#pragma once
#include "camera.h"
#include "deepzoom.h"
#include "fractal_lib.h"
#include "gl_util.h"
#include "palettes.h"
#include "state.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

bool writePngRaw(const std::string& path, int w, int h, const uint8_t* rgbaBottomUp);

// Effective (possibly animated) value of a parameter at time t.
void paramEffective(const Param& p, float t, float out[4]);

struct View3D {
    Vec3 pos, right, up, fwd;
    float tanHalfFov = 0.5f;
    float sceneScale = 3.0f;
    float focusDist = 3.0f;
    float time = 0.0f;
    float animTime = 0.0f;
    int fullW = 1, fullH = 1;  // resolution of the whole image (for tiled renders)
    float formulaP[5][2] = {};  // p1..p5 of the user formula (the landscape can use it)
    float formulaMaxit = 100;   // its "maxit" (the landscape's iteration count)
};

class Renderer {
public:
    bool init(const std::filesystem::path& dataDir, std::string& err);
    void shutdown();

    // Rebuilds display/classic programs and drops cached fractal programs when
    // any file in shaders/ changed. Returns true if something was reloaded.
    bool reloadCoreIfChanged();
    std::string coreError;

    void setPalette(const Palette& p);

    // Fractal programs build asynchronously (parallel shader compile).
    enum class ProgStatus { Compiling, Ready, Error };
    struct FractalPrograms {
        Program trace, probe;
        std::string error;
        bool started = false;
    };
    FractalPrograms& programs(const Fractal& f);  // starts the build if needed
    ProgStatus status(const Fractal& f);
    void warmUp(const std::vector<Fractal>& all);  // start every build at startup
    void dropPrograms(const std::string& key) { progs_.erase(key); }
    void dropAllPrograms() { progs_.clear(); }

    // ---- 3D: one jittered sample added into `target` (RGBA32F, additive)
    void clear3D(RenderTarget& target);
    bool renderSample3D(RenderTarget& target, int sampleIndex, const Fractal& f, const RenderSettings& rs,
                        const View3D& v, const int* scissor = nullptr);

    // ---- 2D: resumable escape-time compute. One call runs up to `chunk` more
    // iterations for every pixel of rows [y0, y0 + rows) of `out` (target pixels).
    // `first` starts the band's orbits. Pixels write `out` when they finish.
    // `stateSlot` picks the orbit-state images: 0 for the main view and posters, 1 for the Julia inset,
    // so the two can progress independently.
    // `reuse`: a finished 1x image of the same view, whose pixels become the center samples.
    // `trips`/`passSlot`: deep zoom's per-pass trip valve and the lag flag it reports in (see classic2d.comp).
    bool dispatch2D(IndexTarget& out, const Classic2DSettings& cs, int y0, int rows, int chunk, bool first, int stateSlot = 0,
                    const IndexTarget* reuse = nullptr, int trips = 0, int passSlot = 0, int bandIter = 0);
    bool passLagged(int passSlot) const { return lagMap_ && lagMap_[passSlot] != 0; }  // valid once that pass has finished
    int bandRowsFor(int width) const;  // rows per band so the orbit state stays small
    bool classicUsesFp64(const Classic2DSettings& cs, int targetH) const;
    // Deep zoom (perturbation, see deepzoom.h): used for the Mandelbrot set and its
    // Julia sets when a pixel is smaller than doubles can resolve, or when forced.
    bool classicUsesDeep(const Classic2DSettings& cs, int targetH) const;
    void setReferenceOrbit(const std::vector<double>& xy);  // uploads Z_0..Z_n
    void setBlaTable(const BlaTable& t);                      // the skip-ahead table that goes with it
    // Series approximation for the current view (or none): `shift` is the current view's center
    // relative to the center the series was made around; `viewHalf` the current image's half
    // extents (plane units) - the series is used only for an image of exactly that size.
    void setSeries(const SeriesResult* s, const double shift[2], const double viewHalf[2]);
    void setBlaDcMax(double d) { blaDcMax_ = d; }  // the table's reach: pixels farther from the reference don't jump
    double blaDcMax() const { return blaDcMax_; }
    const double* deepOffset() const { return deepOffset_; }
    // the series a first pass of a w x h image would use now (null: none), and its center shift
    const SeriesResult* seriesInUse(const Classic2DSettings& cs, int w, int h, double shift[2]) const {
        if (seriesSkip(cs, w, h) <= 0) return nullptr;
        shift[0] = seriesShift_[0], shift[1] = seriesShift_[1];
        return &series_;
    }
    int seriesSkip(const Classic2DSettings& cs, int w, int h) const;  // iterations every pixel starts at (0: none)
    // (view center - reference point), in plane units
    void setDeepOffset(double dx, double dy) { deepOffset_[0] = dx, deepOffset_[1] = dy; }
    int referenceLength() const { return refLen_; }
    // Transpiled user formula (see formula.h); empty to clear. Rebuilds the custom programs.
    void setCustomFormula(const std::string& glsl, int slot = 0);
    // Slot 0 is the main formula (2D view, posters, landscape); slot 1 is the Julia
    // inset's (a formula's @julia partner), used by dispatch2D's stateSlot 1.
    const std::string& customFormulaError(int slot = 0) const { return customError_[slot & 1]; }
    bool hasCustomFormula(int slot = 0) const { return !customGlsl_[slot & 1].empty() && customError_[slot & 1].empty(); }

    // ---- final image: 3D accumulation or 2D iteration buffer -> fbo
    void display(ViewMode mode, const RenderTarget* accum, const IndexTarget* index, const RenderSettings& rs,
                 const Classic2DSettings& cs, float cycleOffset, int outW, int outH, GLuint fbo);

    // ---- probe: async readback of DE at camera and hit distance along a ray
    bool probe(const Fractal& f, const RenderSettings& rs, const View3D& v, const Vec3& dir, float pixelAngle);
    bool fetchProbe(float& deAtCam, float& hitT);  // oldest pending result, in issue order

    // Renders the display pass at w x h and reads it back (RGBA, bottom-up).
    bool readImage(ViewMode mode, const RenderTarget* accum, const IndexTarget* index, const RenderSettings& rs,
                   const Classic2DSettings& cs, float cycleOffset, int w, int h, std::vector<uint8_t>& rgba);

    PassTimer& passTimer(int stateSlot) { return passTimers_[stateSlot & 1]; }  // 2D compute passes

    RenderTarget accum;    // interactive 3D accumulation
    IndexTarget index2D;   // interactive 2D iteration buffer
    GpuTimer timer;

private:
    bool loadCore(std::string& err);
    Program* classicProgram(bool fp64, int customSlot);  // -1: the built-in formulas
    std::filesystem::file_time_type newestShaderTime() const;
    void buildRetroLuts();

    std::filesystem::path dataDir_;
    std::filesystem::file_time_type coreTime_{};
    std::string vert_, common_, raymarch_, probeSrc_, classicSrc_;
    Program displayProg_;
    Program classic_[2][3];  // [fp64][built-in, custom slot 0, custom slot 1]
    bool classicBuilt_[2][3] = {};
    Program classicDeep_;
    bool classicDeepBuilt_ = false;
    GLuint refSsbo_ = 0, blaSsbo_ = 0, landRefSsbo_ = 0;
    int landRefLen_ = 0;
    std::vector<double> landRefKey_;
    void prepareLandscape(Program& p, const Fractal& f);  // its deep-zoom reference orbit (see landscape.glsl)
    int refLen_ = 0;
    std::vector<int> blaOffset_, blaCount_;
    GLuint seriesSsbo_ = 0;
    SeriesResult series_;  // skip = 0: none
    double seriesShift_[2] = {0, 0}, seriesHalf_[2] = {0, 0};
    double blaDcMax_ = 0;
    GLuint lagSsbo_ = 0;           // one flag per pass slot, persistently mapped
    uint32_t* lagMap_ = nullptr;
    const SeriesResult* seriesUploaded_ = nullptr;
    double deepOffset_[2] = {0, 0};
    std::string customGlsl_[2], customError_[2];
    std::unordered_map<std::string, std::unique_ptr<FractalPrograms>> progs_;
    GLuint vao_ = 0, paletteTex_ = 0, retroLut_[2] = {0, 0}, blueNoise_ = 0;
    StateImages state2D_[2];
    PassTimer passTimers_[2];
    RenderTarget probeRT_, shotRT_;
    GLuint probePbo_[2] = {0, 0};
    GLsync probeFence_[2] = {nullptr, nullptr};
    int probeIdx_ = 0;
};
