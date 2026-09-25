#include "sanitize.h"

#include <algorithm>
#include <cmath>

namespace {
// clamp that also repairs NaN/Inf (std::clamp passes NaN straight through)
template <class T>
void fix(T& v, T lo, T hi, T def) {
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(v)) v = def;
    }
    v = std::clamp(v, lo, hi);
}
void fixColor(float* c, const float* def, float hi = 16.0f) {
    for (int k = 0; k < 3; k++) fix(c[k], 0.0f, hi, def[k]);
}
void fixFinite(float& v, float def) {
    if (!std::isfinite(v)) v = def;
}
}  // namespace

void sanitize(RenderSettings& r) {
    const RenderSettings d;
    fix(r.renderMode, 0, 1, d.renderMode);
    fix(r.bounces, 1, 16, d.bounces);
    fix(r.maxSteps, 8, 10000, d.maxSteps);
    fix(r.detail, 0.001f, 16.0f, d.detail);
    fix(r.stepFactor, 0.01f, 1.5f, d.stepFactor);
    fix(r.maxDist, 0.1f, 1000.0f, d.maxDist);
    fix(r.maxSamplesRT, 1, 65536, d.maxSamplesRT);
    fix(r.maxSamplesPT, 1, 1 << 20, d.maxSamplesPT);
    fixFinite(r.sunAzimuth, d.sunAzimuth);
    r.sunAzimuth = std::fmod(std::fmod(r.sunAzimuth, 360.0f) + 360.0f, 360.0f);
    fix(r.sunElevation, -90.0f, 90.0f, d.sunElevation);
    fixColor(r.sunColor, d.sunColor);
    fix(r.sunIntensity, 0.0f, 100.0f, d.sunIntensity);
    fix(r.sunSize, 0.01f, 45.0f, d.sunSize);
    fixColor(r.skyZenith, d.skyZenith);
    fixColor(r.skyHorizon, d.skyHorizon);
    fix(r.skyIntensity, 0.0f, 20.0f, d.skyIntensity);
    fix(r.background, 0, 1, d.background);
    fixColor(r.bgColor, d.bgColor);
    fix(r.aoStrength, 0.0f, 1.0f, d.aoStrength);
    fix(r.fogDensity, 0.0f, 100.0f, d.fogDensity);
    fixColor(r.fogColor, d.fogColor);
    fix(r.glowStrength, 0.0f, 20.0f, d.glowStrength);
    fixColor(r.glowColor, d.glowColor);
    fix(r.floorY, -1e6f, 1e6f, d.floorY);
    fixColor(r.floorColor, d.floorColor, 1.0f);
    fix(r.colorMode, 0, 4, d.colorMode);
    fix(r.colorScale, 1e-4f, 1e4f, d.colorScale);
    fixFinite(r.colorOffset, d.colorOffset);
    fix(r.paletteMix, 0.0f, 1.0f, d.paletteMix);
    fixColor(r.baseColor, d.baseColor, 1.0f);
    fix(r.specular, 0.0f, 1.0f, d.specular);
    fix(r.roughness, 0.001f, 1.0f, d.roughness);
    fix(r.cycleSpeed, -1000.0f, 1000.0f, d.cycleSpeed);
    fix(r.fov, 1.0f, 170.0f, d.fov);
    fix(r.aperture, 0.0f, 10.0f, d.aperture);
    fix(r.focusDist, 1e-7f, 1e7f, d.focusDist);
    fix(r.exposure, 0.001f, 100.0f, d.exposure);
    fix(r.tonemap, 0, 2, d.tonemap);
    fix(r.vignette, 0.0f, 2.0f, d.vignette);
    fix(r.saturation, 0.0f, 4.0f, d.saturation);
    fix(r.retro, 0, 2, d.retro);
    fix(r.pixelSize, 1, 32, d.pixelSize);
    fix(r.scanlines, 0.0f, 1.0f, d.scanlines);
    fix(r.targetFps, 10.0f, 1000.0f, d.targetFps);
    fix(r.stillScale, 0.1f, 4.0f, d.stillScale);
}

void sanitize(Classic2DSettings& c) {
    const Classic2DSettings d;
    fix(c.formula, 0, kClassicFormulaCount - 1, d.formula);
    fix(c.cx, -1e3, 1e3, d.cx);
    fix(c.cy, -1e3, 1e3, d.cy);
    fix(c.height, kMinHeight, 100.0, d.height);
    fix(c.jx, -1e3, 1e3, d.jx);
    fix(c.jy, -1e3, 1e3, d.jy);
    fix(c.maxIter, 1, kMaxIterations, d.maxIter);
    fix(c.bailout, 2.0f, 1e6f, d.bailout);
    fix(c.power, 2, 16, d.power);
    for (auto& v : c.phoenixP) fix(v, -10.0f, 10.0f, 0.0f);
    fix(c.supersample, 1, 4, d.supersample);
    fix(c.fp64, 0, 3, d.fp64);
    fix(c.colorDensity, 1e-3f, 1e3f, d.colorDensity);
    fix(c.insideMode, 0, 2, d.insideMode);
    fixColor(c.insideColor, d.insideColor, 1.0f);
    fix(c.rootSpread, 0.0f, 256.0f, d.rootSpread);
    fix(c.coloring, 0, kColoringModeCount - 1, d.coloring);
    fix(c.trapSize, 0.01f, 100.0f, d.trapSize);
    for (auto& p : c.formulaP)
        for (int k = 0; k < 2; k++) fix(p[k], -1e6f, 1e6f, 0.0f);
}

void sanitize(Camera& cam) {
    const Camera d;
    fix(cam.pos.x, -1e7f, 1e7f, d.pos.x);
    fix(cam.pos.y, -1e7f, 1e7f, d.pos.y);
    fix(cam.pos.z, -1e7f, 1e7f, d.pos.z);
    fixFinite(cam.yaw, 0.0f);
    fix(cam.pitch, -1.55f, 1.55f, 0.0f);
    fix(cam.distance, 1e-7f, 1e7f, d.distance);
}

void sanitize(Fractal& f) {
    for (auto& p : f.params) {
        for (int k = 0; k < 4; k++) fixFinite(p.value[k], p.def[k]);
        switch (p.type) {
        case ParamType::Bool: p.value[0] = p.value[0] > 0.5f ? 1.0f : 0.0f; break;
        case ParamType::Choice:
        case ParamType::Int: p.value[0] = std::clamp(std::round(p.value[0]), p.minV, p.maxV); break;
        case ParamType::Color:
            for (int k = 0; k < 3; k++) p.value[k] = std::clamp(p.value[k], 0.0f, 1.0f);
            break;
        default: break;  // floats/vectors may deliberately go past the slider range
        }
        fix(p.animSpeed, 0.0f, 10.0f, 0.15f);
        fix(p.animDepth, 0.0f, 1.0f, 0.25f);
    }
}
