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

    struct FractalPrograms {
        Program trace, probe;
        std::string error;
        bool attempted = false;
    };
    FractalPrograms& programs(const Fractal& f);
    void dropPrograms(const std::string& key) { progs_.erase(key); }
    void dropAllPrograms() { progs_.clear(); }

    // ---- 3D: one jittered sample added into `target` (RGBA32F, additive)
    void clear3D(RenderTarget& target);
    bool renderSample3D(RenderTarget& target, int sampleIndex, const Fractal& f, const RenderSettings& rs,
                        const View3D& v, const int* scissor = nullptr);

    // ---- 2D: iteration buffer at (w*ss) x (h*ss)
    bool render2D(RenderTarget& target, const Classic2DSettings& cs, int outW, int outH, const int* scissor = nullptr);
    bool classicUsesFp64(const Classic2DSettings& cs, int outH) const;

    // ---- final image
    void display(ViewMode mode, const RenderTarget& src, const RenderSettings& rs, const Classic2DSettings& cs,
                 float cycleOffset, int outW, int outH, GLuint fbo);

    // ---- probe: async readback of DE at camera and hit distance along a ray
    bool probe(const Fractal& f, const RenderSettings& rs, const View3D& v, const Vec3& dir, float pixelAngle);
    bool fetchProbe(float& deAtCam, float& hitT);  // oldest pending result, in issue order

    bool writePng(const std::string& path, ViewMode mode, const RenderTarget& src, const RenderSettings& rs,
                  const Classic2DSettings& cs, float cycleOffset, int w, int h);

    RenderTarget accum;   // interactive 3D accumulation
    RenderTarget index2D; // interactive 2D iteration buffer
    GpuTimer timer;

private:
    bool loadCore(std::string& err);
    std::filesystem::file_time_type newestShaderTime() const;

    std::filesystem::path dataDir_;
    std::filesystem::file_time_type coreTime_{};
    std::string vert_, common_, raymarch_, probeSrc_, classic_, display_;
    Program displayProg_, classic32_, classic64_;
    std::unordered_map<std::string, std::unique_ptr<FractalPrograms>> progs_;
    GLuint vao_ = 0, paletteTex_ = 0, retroTex_ = 0;
    RenderTarget probeRT_, shotRT_;
    GLuint probePbo_[2] = {0, 0};
    GLsync probeFence_[2] = {nullptr, nullptr};
    int probeIdx_ = 0;
};
