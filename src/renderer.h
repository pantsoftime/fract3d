#pragma once
#include "camera.h"
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
    bool dispatch2D(IndexTarget& out, const Classic2DSettings& cs, int y0, int rows, int chunk, bool first);
    int bandRowsFor(int width) const;  // rows per band so the orbit state stays small
    bool classicUsesFp64(const Classic2DSettings& cs, int targetH) const;
    // Transpiled user formula (see formula.h); empty to clear. Rebuilds the custom programs.
    void setCustomFormula(const std::string& glsl);
    const std::string& customFormulaError() const { return customError_; }
    bool hasCustomFormula() const { return !customGlsl_.empty() && customError_.empty(); }

    // ---- final image: 3D accumulation or 2D iteration buffer -> fbo
    void display(ViewMode mode, const RenderTarget* accum, const IndexTarget* index, const RenderSettings& rs,
                 const Classic2DSettings& cs, float cycleOffset, int outW, int outH, GLuint fbo);

    // ---- probe: async readback of DE at camera and hit distance along a ray
    bool probe(const Fractal& f, const RenderSettings& rs, const View3D& v, const Vec3& dir, float pixelAngle);
    bool fetchProbe(float& deAtCam, float& hitT);  // oldest pending result, in issue order

    // Renders the display pass at w x h and reads it back (RGBA, bottom-up).
    bool readImage(ViewMode mode, const RenderTarget* accum, const IndexTarget* index, const RenderSettings& rs,
                   const Classic2DSettings& cs, float cycleOffset, int w, int h, std::vector<uint8_t>& rgba);

    RenderTarget accum;    // interactive 3D accumulation
    IndexTarget index2D;   // interactive 2D iteration buffer
    GpuTimer timer;

private:
    bool loadCore(std::string& err);
    Program* classicProgram(bool fp64, bool custom);
    std::filesystem::file_time_type newestShaderTime() const;
    void buildRetroLuts();

    std::filesystem::path dataDir_;
    std::filesystem::file_time_type coreTime_{};
    std::string vert_, common_, raymarch_, probeSrc_, classicSrc_;
    Program displayProg_;
    Program classic_[2][2];  // [fp64][custom formula]
    bool classicBuilt_[2][2] = {};
    std::string customGlsl_, customError_;
    std::unordered_map<std::string, std::unique_ptr<FractalPrograms>> progs_;
    GLuint vao_ = 0, paletteTex_ = 0, retroLut_[2] = {0, 0};
    StateImages state2D_;
    RenderTarget probeRT_, shotRT_;
    GLuint probePbo_[2] = {0, 0};
    GLsync probeFence_[2] = {nullptr, nullptr};
    int probeIdx_ = 0;
};
