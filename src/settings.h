#pragma once
// One table of every user-facing setting. Everything that needs to enumerate
// settings derives from it: PAR files, @look keys, the "did the image change?"
// signatures that restart progressive rendering, and the performance prefs.
#include "state.h"

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

enum FieldFlags : unsigned {
    kTrace3D = 1u << 0,    // changes the 3D image: restart accumulation
    kCompute2D = 1u << 1,  // changes the 2D iteration buffer: recompute
    kDisplay = 1u << 2,    // only changes how the result is displayed
    kLook = 1u << 3,       // part of a fractal's curated look (@look); reset when switching fractals
    kLighting = 1u << 4,   // the lighting part of the look (kept with "keep my lighting")
    kPar = 1u << 5,        // saved in PAR files
    kPerf = 1u << 6,       // performance preference (prefs.ini, not PAR)
    kCount = 1u << 7,      // an int that's a quantity (not a choice): camera paths interpolate it
    kLogScale = 1u << 8,   // ...in log space (iteration limits span orders of magnitude)
};

// f(const char* parName, const char* lookAlias /*or nullptr*/, T* ptr, int count, unsigned flags)
// with T one of bool, int, float, double.
template <class F>
void visitRender(RenderSettings& r, F&& f) {
    constexpr unsigned T = kTrace3D | kPar, TL = T | kLook, TLL = TL | kLighting, D = kDisplay | kPar;
    f("render.mode", nullptr, &r.renderMode, 1, T);
    f("render.bounces", nullptr, &r.bounces, 1, T | kCount);
    f("render.maxSteps", nullptr, &r.maxSteps, 1, T | kCount);
    f("render.detail", nullptr, &r.detail, 1, T);
    f("render.stepFactor", nullptr, &r.stepFactor, 1, T);
    f("render.maxDist", nullptr, &r.maxDist, 1, T);
    f("render.maxSamplesRT", nullptr, &r.maxSamplesRT, 1, kPar | kCount | kLogScale);
    f("render.maxSamplesPT", nullptr, &r.maxSamplesPT, 1, kPar | kCount | kLogScale);
    f("light.sunAzimuth", "sunAzimuth", &r.sunAzimuth, 1, TLL);
    f("light.sunElevation", "sunElevation", &r.sunElevation, 1, TLL);
    f("light.sunColor", "sunColor", r.sunColor, 3, TLL);
    f("light.sunIntensity", "sunIntensity", &r.sunIntensity, 1, TLL);
    f("light.sunSize", "sunSize", &r.sunSize, 1, TLL);
    f("light.shadows", nullptr, &r.shadows, 1, T);
    f("light.skyZenith", "skyZenith", r.skyZenith, 3, TLL);
    f("light.skyHorizon", "skyHorizon", r.skyHorizon, 3, TLL);
    f("light.skyIntensity", "sky", &r.skyIntensity, 1, TLL);
    f("light.background", "background", &r.background, 1, TLL);
    f("light.bgColor", "bgColor", r.bgColor, 3, TLL);
    f("light.ao", "ao", &r.aoStrength, 1, TLL);
    f("light.fogDensity", "fog", &r.fogDensity, 1, TLL);
    f("light.fogColor", "fogColor", r.fogColor, 3, TLL);
    f("light.glow", "glow", &r.glowStrength, 1, TLL);
    f("light.glowColor", "glowColor", r.glowColor, 3, TLL);
    f("light.floor", "floor", &r.floorOn, 1, TL);
    f("light.floorY", "floorY", &r.floorY, 1, TL);
    f("light.floorColor", "floorColor", r.floorColor, 3, TL);
    f("color.mode", "colorMode", &r.colorMode, 1, TL);
    f("color.scale", "colorScale", &r.colorScale, 1, TL);
    f("color.offset", "colorOffset", &r.colorOffset, 1, TL);
    f("color.paletteMix", "paletteMix", &r.paletteMix, 1, TL);
    f("color.base", "baseColor", r.baseColor, 3, TL);
    f("color.specular", "specular", &r.specular, 1, TL);
    f("color.roughness", "roughness", &r.roughness, 1, TL);
    f("color.cycleSpeed", nullptr, &r.cycleSpeed, 1, kDisplay | kPar);
    f("camera.fov", "fov", &r.fov, 1, TL);
    f("camera.aperture", nullptr, &r.aperture, 1, T);
    f("camera.autoFocus", nullptr, &r.autoFocus, 1, T);
    f("camera.focusDist", nullptr, &r.focusDist, 1, T);
    f("post.exposure", nullptr, &r.exposure, 1, D);
    f("post.tonemap", nullptr, &r.tonemap, 1, D);
    f("post.vignette", nullptr, &r.vignette, 1, D);
    f("post.saturation", nullptr, &r.saturation, 1, D);
    f("post.retro", nullptr, &r.retro, 1, D);
    f("post.pixelSize", nullptr, &r.pixelSize, 1, D | kCount);
    f("post.scanlines", nullptr, &r.scanlines, 1, D);
    f("perf.adaptiveRes", nullptr, &r.adaptiveRes, 1, kPerf);
    f("perf.targetFps", nullptr, &r.targetFps, 1, kPerf);
    f("perf.stillScale", nullptr, &r.stillScale, 1, kPerf);
}

template <class F>
void visitClassic(Classic2DSettings& c, F&& f) {
    constexpr unsigned C = kCompute2D | kPar, D = kDisplay | kPar;
    f("classic.formula", nullptr, &c.formula, 1, C);
    f("classic.julia", nullptr, &c.julia, 1, C);
    f("classic.center", nullptr, &c.cx, 2, C);  // cx, cy are adjacent doubles
    f("classic.height", nullptr, &c.height, 1, C);
    f("classic.juliaC", nullptr, &c.jx, 2, C);
    f("classic.maxIter", nullptr, &c.maxIter, 1, C | kCount | kLogScale);
    f("classic.bailout", nullptr, &c.bailout, 1, C);
    f("classic.power", nullptr, &c.power, 1, C);
    f("classic.phoenixP", nullptr, c.phoenixP, 2, C);
    f("classic.fp64", nullptr, &c.fp64, 1, C);
    f("classic.bla", nullptr, &c.bla, 1, C);
    f("classic.periodicity", nullptr, &c.periodicity, 1, C);
    f("classic.series", nullptr, &c.series, 1, C);
    f("classic.banded", nullptr, &c.banded, 1, C);
    f("classic.coloring", nullptr, &c.coloring, 1, C);
    f("classic.trapSize", nullptr, &c.trapSize, 1, C);
    f("classic.p1", nullptr, c.formulaP[0], 2, C);
    f("classic.p2", nullptr, c.formulaP[1], 2, C);
    f("classic.p3", nullptr, c.formulaP[2], 2, C);
    f("classic.p4", nullptr, c.formulaP[3], 2, C);
    f("classic.p5", nullptr, c.formulaP[4], 2, C);
    f("classic.supersample", nullptr, &c.supersample, 1, D);
    f("classic.colorDensity", nullptr, &c.colorDensity, 1, D);
    f("classic.insideMode", nullptr, &c.insideMode, 1, D);
    f("classic.insideColor", nullptr, c.insideColor, 3, D);
    f("classic.rootSpread", nullptr, &c.rootSpread, 1, D);
    f("classic.showOrbit", nullptr, &c.showOrbit, 1, D);
}

// Appends the values of every field matching `mask` — memberwise, so padding and
// field types don't matter (unlike hashing the raw struct).
template <class S, class Visit>
void appendFields(std::vector<uint8_t>& out, S& s, unsigned mask, Visit visit) {
    visit(s, [&](const char*, const char*, auto* ptr, int n, unsigned flags) {
        if (!(flags & mask)) return;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(ptr);
        out.insert(out.end(), p, p + sizeof(*ptr) * n);
    });
}

// Copies every field matching `mask` from src to dst.
template <class S, class Visit>
void copyFields(S& dst, const S& src, unsigned mask, Visit visit) {
    visit(dst, [&](const char*, const char*, auto* ptr, int n, unsigned flags) {
        if (!(flags & mask)) return;
        size_t off = reinterpret_cast<const char*>(ptr) - reinterpret_cast<const char*>(&dst);
        std::memcpy(ptr, reinterpret_cast<const char*>(&src) + off, sizeof(*ptr) * n);
    });
}
