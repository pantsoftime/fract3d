// PAR files: human-readable "key = value" snapshots of a whole view, named after
// Fractint's .PAR parameter files. Every screenshot writes one next to the PNG.
#include "app.h"
#include "sanitize.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

// Visits every persisted setting as (name, pointer, component count).
template <class F>
static void visitSettings(RenderSettings& r, Classic2DSettings& c, F&& f) {
    f("render.mode", &r.renderMode, 1);
    f("render.bounces", &r.bounces, 1);
    f("render.maxSteps", &r.maxSteps, 1);
    f("render.detail", &r.detail, 1);
    f("render.stepFactor", &r.stepFactor, 1);
    f("render.maxDist", &r.maxDist, 1);
    f("render.maxSamplesRT", &r.maxSamplesRT, 1);
    f("render.maxSamplesPT", &r.maxSamplesPT, 1);
    f("light.sunAzimuth", &r.sunAzimuth, 1);
    f("light.sunElevation", &r.sunElevation, 1);
    f("light.sunColor", r.sunColor, 3);
    f("light.sunIntensity", &r.sunIntensity, 1);
    f("light.sunSize", &r.sunSize, 1);
    f("light.shadows", &r.shadows, 1);
    f("light.skyZenith", r.skyZenith, 3);
    f("light.skyHorizon", r.skyHorizon, 3);
    f("light.skyIntensity", &r.skyIntensity, 1);
    f("light.background", &r.background, 1);
    f("light.bgColor", r.bgColor, 3);
    f("light.ao", &r.aoStrength, 1);
    f("light.fogDensity", &r.fogDensity, 1);
    f("light.fogColor", r.fogColor, 3);
    f("light.glow", &r.glowStrength, 1);
    f("light.glowColor", r.glowColor, 3);
    f("light.floor", &r.floorOn, 1);
    f("light.floorY", &r.floorY, 1);
    f("light.floorColor", r.floorColor, 3);
    f("color.mode", &r.colorMode, 1);
    f("color.scale", &r.colorScale, 1);
    f("color.offset", &r.colorOffset, 1);
    f("color.paletteMix", &r.paletteMix, 1);
    f("color.base", r.baseColor, 3);
    f("color.specular", &r.specular, 1);
    f("color.roughness", &r.roughness, 1);
    f("color.cycleSpeed", &r.cycleSpeed, 1);
    f("camera.fov", &r.fov, 1);
    f("camera.aperture", &r.aperture, 1);
    f("camera.autoFocus", &r.autoFocus, 1);
    f("camera.focusDist", &r.focusDist, 1);
    f("post.exposure", &r.exposure, 1);
    f("post.tonemap", &r.tonemap, 1);
    f("post.vignette", &r.vignette, 1);
    f("post.saturation", &r.saturation, 1);
    f("post.retro", &r.retro, 1);
    f("post.pixelSize", &r.pixelSize, 1);
    f("post.scanlines", &r.scanlines, 1);
    f("classic.formula", &c.formula, 1);
    f("classic.julia", &c.julia, 1);
    f("classic.center", &c.cx, 2);  // cx, cy are adjacent doubles
    f("classic.height", &c.height, 1);
    f("classic.juliaC", &c.jx, 2);
    f("classic.maxIter", &c.maxIter, 1);
    f("classic.bailout", &c.bailout, 1);
    f("classic.power", &c.power, 1);
    f("classic.phoenixP", c.phoenixP, 2);
    f("classic.supersample", &c.supersample, 1);
    f("classic.banded", &c.banded, 1);
    f("classic.colorDensity", &c.colorDensity, 1);
    f("classic.insideMode", &c.insideMode, 1);
    f("classic.insideColor", c.insideColor, 3);
    f("classic.rootSpread", &c.rootSpread, 1);
}

static void writeVals(std::ostream& o, const int* p, int n) { for (int i = 0; i < n; i++) o << (i ? " " : "") << p[i]; }
static void writeVals(std::ostream& o, const float* p, int n) { for (int i = 0; i < n; i++) o << (i ? " " : "") << p[i]; }
static void writeVals(std::ostream& o, const double* p, int n) {
    char buf[40];
    for (int i = 0; i < n; i++) {
        snprintf(buf, sizeof buf, "%.17g", p[i]);
        o << (i ? " " : "") << buf;
    }
}
static void readVals(std::istream& is, int* p, int n) { for (int i = 0; i < n; i++) is >> p[i]; }
static void readVals(std::istream& is, float* p, int n) { for (int i = 0; i < n; i++) is >> p[i]; }
static void readVals(std::istream& is, double* p, int n) { for (int i = 0; i < n; i++) is >> p[i]; }

bool App::savePar(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    if (!out || !(out << parText())) {
        toast("Could not write " + path.string());
        return false;
    }
    toast("Saved " + path.string());
    return true;
}

std::string App::parText() const {
    std::ostringstream o;
    o.precision(9);
    const Fractal& f = lib_.all()[current_];
    RenderSettings r = rs;
    Classic2DSettings c = cs;
    o << "; Fract3D parameter file - load with File > Load, or: fract3d --par FILE\n";
    o << "mode = " << (mode == ViewMode::Classic2D ? "2d" : "3d") << "\n";
    o << "fractal = " << f.key << "\n";
    o << "camera.pos = " << cam.pos.x << " " << cam.pos.y << " " << cam.pos.z << "\n";
    o << "camera.yaw = " << cam.yaw << "\ncamera.pitch = " << cam.pitch << "\ncamera.distance = " << cam.distance << "\n";
    o << "color.palette = " << palettes[rs.palette].name << "\n";
    if (rs.palette == customPaletteIdx) {
        o << "color.cosine =";
        for (auto* arr : {customCosine.a, customCosine.b, customCosine.c, customCosine.d})
            for (int k = 0; k < 3; k++) o << " " << arr[k];
        o << "\n";
    }
    for (auto& p : f.params) {
        o << "param." << p.id << " = ";
        writeVals(o, p.value, p.components());
        o << "\n";
        if (p.animate) o << "param." << p.id << ".anim = " << p.animSpeed << " " << p.animDepth << "\n";
    }
    visitSettings(r, c, [&](const char* name, auto* ptr, int n) {
        o << name << " = ";
        writeVals(o, ptr, n);
        o << "\n";
    });
    return o.str();
}

bool App::loadPar(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::map<std::string, std::string> kv;
    std::string line, note;
    while (std::getline(in, line)) {
        if (!line.empty() && line[0] == ';' && note.empty() && line.find("Fract3D parameter file") == std::string::npos)
            note = line.substr(line.find_first_not_of("; ") == std::string::npos ? 1 : line.find_first_not_of("; "));
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        size_t e = line.find('=');
        if (e == std::string::npos) continue;
        auto trim = [](std::string s) {
            size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        };
        kv[trim(line.substr(0, e))] = trim(line.substr(e + 1));
    }
    if (kv.count("fractal")) {
        int idx = lib_.indexOf(kv["fractal"]);
        if (idx < 0) {
            toast("PAR needs fractal '" + kv["fractal"] + "', which isn't installed");
            return false;
        }
        selectFractal(idx, false);
    }
    // Start from the fractal's defaults and curated look, so a hand-written PAR
    // only needs the keys it wants to change. Saved PARs contain every key anyway.
    Fractal& f = fractal();
    rs = RenderSettings();
    rs.stepFactor = f.hints.stepFactor;
    rs.detail = f.hints.detail;
    rs.maxSteps = f.hints.maxSteps;
    rs.maxDist = f.hints.maxDist;
    for (auto& p : f.params) p.reset();
    resetView();
    applyLook(f);

    setMode(kv["mode"] == "2d" ? ViewMode::Classic2D : ViewMode::Fractal3D);
    if (kv.count("camera.pos")) {
        std::istringstream is(kv["camera.pos"]);
        is >> cam.pos.x >> cam.pos.y >> cam.pos.z;
    }
    if (kv.count("camera.yaw")) cam.yaw = std::stof(kv["camera.yaw"]);
    if (kv.count("camera.pitch")) cam.pitch = std::stof(kv["camera.pitch"]);
    if (kv.count("camera.distance")) cam.distance = std::stof(kv["camera.distance"]);
    if (kv.count("camera.target")) {  // convenient for hand-written presets: aim at a point
        Vec3 t;
        std::istringstream is(kv["camera.target"]);
        is >> t.x >> t.y >> t.z;
        cam.lookAt(cam.pos, t);
    }
    for (auto& p : f.params) {
        auto it = kv.find("param." + p.id);
        if (it != kv.end()) {
            std::istringstream is(it->second);
            readVals(is, p.value, p.components());
        }
        auto an = kv.find("param." + p.id + ".anim");
        if (an != kv.end()) {
            std::istringstream is(an->second);
            is >> p.animSpeed >> p.animDepth;
            p.animate = true;
        }
    }
    visitSettings(rs, cs, [&](const char* name, auto* ptr, int n) {
        auto it = kv.find(name);
        if (it == kv.end()) return;
        std::istringstream is(it->second);
        readVals(is, ptr, n);
    });
    if (kv.count("color.cosine")) {
        std::istringstream is(kv["color.cosine"]);
        for (auto* arr : {customCosine.a, customCosine.b, customCosine.c, customCosine.d})
            for (int k = 0; k < 3; k++) is >> arr[k];
    }
    if (kv.count("color.palette")) {
        for (int i = 0; i < (int)palettes.size(); i++)
            if (palettes[i].name == kv["color.palette"]) rs.palette = i;
    }
    // Everything above came from a file: repair anything out of range before use.
    sanitize(rs);
    sanitize(cs);
    sanitize(cam);
    sanitize(f);
    applyPalette();
    parNote = note;
    toast(note.empty() ? "Loaded " + path.filename().string() : note, note.empty() ? 2.5f : 6.0f);
    return true;
}

// Steps through the built-in presets (the Learn panel's Tour tab, or N / Shift+N).
void App::tourStep(int delta) {
    std::vector<fs::path> tour;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dataDir / "presets", ec))
        if (e.path().extension() == ".par") tour.push_back(e.path());
    if (tour.empty()) return;
    std::sort(tour.begin(), tour.end());
    tourIdx = ((tourIdx + delta) % (int)tour.size() + (int)tour.size()) % (int)tour.size();
    loadPar(tour[tourIdx]);
}

std::vector<fs::path> App::listParFiles() const {
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto dir : {dataDir / "presets", userDir / "params"})
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".par") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}
