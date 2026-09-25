// PAR files: human-readable "key = value" snapshots of a whole view, named after
// Fractint's .PAR parameter files. Every screenshot writes one next to the PNG.
#include "app.h"
#include "sanitize.h"
#include "settings.h"

#include <algorithm>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

// Every setting flagged kPar in settings.h, as (name, pointer, component count).
template <class F>
static void visitSettings(RenderSettings& r, Classic2DSettings& c, F&& f) {
    auto pick = [&](const char* name, const char*, auto* ptr, int n, unsigned flags) {
        if (flags & kPar) f(name, ptr, n);
    };
    visitRender(r, pick);
    visitClassic(c, pick);
}

static void writeVals(std::ostream& o, const bool* p, int n) { for (int i = 0; i < n; i++) o << (i ? " " : "") << (p[i] ? 1 : 0); }
static void readVals(std::istream& is, bool* p, int n) {
    for (int i = 0; i < n; i++) {
        int v = 0;
        is >> v;
        p[i] = v != 0;
    }
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
    const Fractal& f = lib_.all()[view.fractal];
    RenderSettings r = view.rs;
    Classic2DSettings c = view.cs;
    o << "; Fract3D parameter file - load with File > Load, or: fract3d --par FILE\n";
    o << "mode = " << (view.mode == ViewMode::Classic2D ? "2d" : "3d") << "\n";
    o << "fractal = " << f.key << "\n";
    o << "camera.pos = " << view.cam.pos.x << " " << view.cam.pos.y << " " << view.cam.pos.z << "\n";
    o << "camera.yaw = " << view.cam.yaw << "\ncamera.pitch = " << view.cam.pitch << "\ncamera.distance = " << view.cam.distance << "\n";
    o << "color.palette = " << palettes[view.rs.palette].name << "\n";
    if (view.rs.palette == customPaletteIdx) {
        o << "color.cosine =";
        for (auto* arr : {view.cosine.a, view.cosine.b, view.cosine.c, view.cosine.d})
            for (int k = 0; k < 3; k++) o << " " << arr[k];
        o << "\n";
    }
    if (view.mode == ViewMode::Classic2D && view.cs.formula == kCustomFormula) {
        std::string src = view.formulaSource;  // one line: newlines become \n
        std::string esc;
        for (char ch : src) esc += ch == '\n' ? std::string("\\n") : ch == '\\' ? std::string("\\\\") : std::string(1, ch);
        o << "formula.name = " << view.formulaName << "\n";
        o << "formula.source = " << esc << "\n";
        o << "formula.fn = " << view.fn[0] << " " << view.fn[1] << " " << view.fn[2] << " " << view.fn[3] << "\n";
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
    view.rs = RenderSettings();
    view.rs.stepFactor = f.hints.stepFactor;
    view.rs.detail = f.hints.detail;
    view.rs.maxSteps = f.hints.maxSteps;
    view.rs.maxDist = f.hints.maxDist;
    for (auto& p : f.params) p.reset();
    resetView();
    applyLook(f);

    setMode(kv["mode"] == "2d" ? ViewMode::Classic2D : ViewMode::Fractal3D);
    if (kv.count("camera.pos")) {
        std::istringstream is(kv["camera.pos"]);
        is >> view.cam.pos.x >> view.cam.pos.y >> view.cam.pos.z;
    }
    if (kv.count("camera.yaw")) view.cam.yaw = std::stof(kv["camera.yaw"]);
    if (kv.count("camera.pitch")) view.cam.pitch = std::stof(kv["camera.pitch"]);
    if (kv.count("camera.distance")) view.cam.distance = std::stof(kv["camera.distance"]);
    if (kv.count("camera.target")) {  // convenient for hand-written presets: aim at a point
        Vec3 t;
        std::istringstream is(kv["camera.target"]);
        is >> t.x >> t.y >> t.z;
        view.cam.lookAt(view.cam.pos, t);
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
    visitSettings(view.rs, view.cs, [&](const char* name, auto* ptr, int n) {
        auto it = kv.find(name);
        if (it == kv.end()) return;
        std::istringstream is(it->second);
        readVals(is, ptr, n);
    });
    if (kv.count("color.cosine")) {
        std::istringstream is(kv["color.cosine"]);
        for (auto* arr : {view.cosine.a, view.cosine.b, view.cosine.c, view.cosine.d})
            for (int k = 0; k < 3; k++) is >> arr[k];
    }
    if (kv.count("color.palette")) {
        for (int i = 0; i < (int)palettes.size(); i++)
            if (palettes[i].name == kv["color.palette"]) view.rs.palette = i;
    }
    if (kv.count("formula.fn")) {
        std::istringstream is(kv["formula.fn"]);
        for (int& x : view.fn) is >> x;
        for (int& x : view.fn) x = std::clamp(x, 0, kFormulaFunctionCount - 1);
    }
    if (kv.count("formula.source")) {
        std::string src, esc = kv["formula.source"];
        for (size_t i = 0; i < esc.size(); i++) {
            if (esc[i] == '\\' && i + 1 < esc.size()) {
                src += esc[i + 1] == 'n' ? '\n' : esc[i + 1];
                i++;
            } else {
                src += esc[i];
            }
        }
        view.formulaSource = src;
        ui.formulaEdit = src;
        if (kv.count("formula.name")) view.formulaName = kv["formula.name"];
        if (!compileFormula()) toast("The formula in this PAR has an error: " + formulaError, 6);
    } else if (kv.count("formula.name")) {
        selectFormula(kv["formula.name"]);
    }
    // Everything above came from a file: repair anything out of range before use.
    sanitize(view.rs);
    sanitize(view.cs);
    sanitize(view.cam);
    sanitize(f);
    applyPalette();
    ui.parNote = note;
    toast(note.empty() ? "Loaded " + path.filename().string() : note, note.empty() ? 2.5f : 6.0f);
    return true;
}

void App::saveNamedPar() {
    std::string name = ui.parName;
    // keep the name a plain file name; fall back to a timestamp when it's empty
    for (auto& c : name)
        if (c == '/' || c == '\\' || (unsigned char)c < 32) c = '_';
    if (name.find_first_not_of(" ._") == std::string::npos) name = "view-" + timestampName();
    savePar(session.userDir / "params" / (name + ".par"));
}

// Steps through the built-in presets (the Learn panel's Tour tab, or N / Shift+N).
void App::tourStep(int delta) {
    int n = (int)ui.tourStops.size();
    if (n == 0) return;
    ui.tourIdx = ((ui.tourIdx + delta) % n + n) % n;
    loadPar(ui.tourStops[ui.tourIdx].first);
}

std::vector<fs::path> App::listParFiles() const {
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto dir : {session.dataDir / "presets", session.userDir / "params"})
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".par") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}
