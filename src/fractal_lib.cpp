#include "fractal_lib.h"

#include "gl_util.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace fs = std::filesystem;

int Param::components() const {
    switch (type) {
    case ParamType::Vec2: return 2;
    case ParamType::Vec3:
    case ParamType::Color: return 3;
    case ParamType::Vec4: return 4;
    default: return 1;
    }
}

const char* Param::glslType() const {
    switch (type) {
    case ParamType::Float: return "float";
    case ParamType::Int:
    case ParamType::Choice: return "int";
    case ParamType::Bool: return "bool";
    case ParamType::Vec2: return "vec2";
    case ParamType::Vec3:
    case ParamType::Color: return "vec3";
    case ParamType::Vec4: return "vec4";
    }
    return "float";
}

void Param::reset() {
    for (int i = 0; i < 4; i++) value[i] = def[i], exact[i] = def[i];
    animate = false;
}

Param* Fractal::find(const std::string& id) {
    for (auto& p : params)
        if (p.id == id) return &p;
    return nullptr;
}
const Param* Fractal::find(const std::string& id) const { return const_cast<Fractal*>(this)->find(id); }

std::string Fractal::uniformDecls() const {
    std::string s;
    for (auto& p : params) s += std::string("uniform ") + p.glslType() + " " + p.id + ";\n";
    return s;
}

// ------------------------------------------------------------------ parsing helpers
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// Reads a scalar "1.5" or a tuple "(1, 2, 3)" from s at position i.
static int readNumbers(const std::string& s, size_t& i, float* out, int maxN) {
    while (i < s.size() && isspace((unsigned char)s[i])) i++;
    int n = 0;
    if (i < s.size() && s[i] == '(') {
        i++;
        while (i < s.size() && s[i] != ')' && n < maxN) {
            char* end = nullptr;
            out[n++] = strtof(s.c_str() + i, &end);
            i = end - s.c_str();
            while (i < s.size() && (isspace((unsigned char)s[i]) || s[i] == ',')) i++;
        }
        if (i < s.size() && s[i] == ')') i++;
        return n;
    }
    if (startsWith(s.substr(i), "true")) { out[0] = 1; i += 4; return 1; }
    if (startsWith(s.substr(i), "false")) { out[0] = 0; i += 5; return 1; }
    char* end = nullptr;
    out[0] = strtof(s.c_str() + i, &end);
    if (end == s.c_str() + i) return 0;
    i = end - s.c_str();
    return 1;
}

// @param TYPE ID = DEFAULT [MIN, MAX] [log] {A, B} "Label" -- description
static bool parseParam(const std::string& line, Param& p, std::string& err) {
    std::istringstream is(line);
    std::string type, id, eq;
    is >> type >> id >> eq;
    if (eq != "=") { err = "expected '=' in @param " + id; return false; }
    if (type == "float") p.type = ParamType::Float;
    else if (type == "int") p.type = ParamType::Int;
    else if (type == "bool") p.type = ParamType::Bool;
    else if (type == "vec2") p.type = ParamType::Vec2;
    else if (type == "vec3") p.type = ParamType::Vec3;
    else if (type == "vec4") p.type = ParamType::Vec4;
    else if (type == "choice") p.type = ParamType::Choice;
    else if (type == "color") p.type = ParamType::Color;
    else { err = "unknown @param type '" + type + "'"; return false; }
    p.id = id;
    p.label = id;

    size_t i = line.find('=') + 1;
    int n = readNumbers(line, i, p.def, 4);
    if (n == 1 && p.components() > 1)
        for (int k = 1; k < 4; k++) p.def[k] = p.def[0];
    if (n == 0) { err = "bad default for @param " + id; return false; }
    if (p.type == ParamType::Color) { p.minV = 0; p.maxV = 1; }
    if (p.type == ParamType::Bool) { p.minV = 0; p.maxV = 1; }

    std::string rest = line.substr(i);
    size_t dd = rest.find("--");
    if (dd != std::string::npos) {
        p.desc = trim(rest.substr(dd + 2));
        rest = rest.substr(0, dd);
    }
    size_t lb = rest.find('['), rb = rest.find(']');
    if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
        std::string r = rest.substr(lb + 1, rb - lb - 1);
        std::replace(r.begin(), r.end(), ',', ' ');
        std::istringstream rs(r);
        rs >> p.minV >> p.maxV;
        if (p.maxV < p.minV) std::swap(p.minV, p.maxV);  // tolerate a reversed [max, min]
    }
    std::string beforeLabel = rest.substr(0, rest.find('"'));
    if (beforeLabel.find(" log") != std::string::npos) p.logScale = true;
    if (beforeLabel.find(" hires") != std::string::npos) p.hires = true;
    size_t lc = rest.find('{'), rc = rest.find('}');
    if (lc != std::string::npos && rc != std::string::npos && rc > lc) {
        std::string c = rest.substr(lc + 1, rc - lc - 1);
        std::stringstream cs(c);
        std::string item;
        while (std::getline(cs, item, ',')) p.choices.push_back(trim(item));
        p.minV = 0;
        p.maxV = (float)p.choices.size() - 1;
    }
    size_t q1 = rest.find('"');
    if (q1 != std::string::npos) {
        size_t q2 = rest.find('"', q1 + 1);
        if (q2 != std::string::npos) p.label = rest.substr(q1 + 1, q2 - q1 - 1);
    }
    for (int k = 0; k < 4; k++) p.value[k] = p.def[k], p.exact[k] = p.def[k];
    return true;
}

static void parseRenderHints(const std::string& s, RenderHints& h) {
    std::istringstream is(s);
    std::string kv;
    while (is >> kv) {
        size_t e = kv.find('=');
        if (e == std::string::npos) continue;
        std::string k = kv.substr(0, e);
        float v = strtof(kv.c_str() + e + 1, nullptr);
        if (k == "stepFactor") h.stepFactor = v;
        else if (k == "detail") h.detail = v;
        else if (k == "maxDist") h.maxDist = v;
        else if (k == "maxSteps") h.maxSteps = (int)v;
    }
}

bool parseFractalFile(const fs::path& path, Fractal& f) {
    bool ok = false;
    std::string text = readTextFile(path.string(), &ok);
    if (!ok) {
        f.parseError = "cannot read " + path.string();
        return false;
    }
    f.path = path;
    f.key = path.stem().string();
    f.name = f.key;
    f.params.clear();
    f.look.clear();
    f.about.clear();
    f.parseError.clear();
    std::error_code ec;
    f.mtime = fs::last_write_time(path, ec);

    std::istringstream is(text);
    std::string line, code;
    bool inAbout = false;
    int lineNo = 0;
    while (std::getline(is, line)) {
        lineNo++;
        std::string t = trim(line);
        if (startsWith(t, "//")) {
            std::string c = t.substr(2);
            if (!c.empty() && c[0] == ' ') c = c.substr(1);
            if (inAbout) {
                if (startsWith(c, "@end")) inAbout = false;
                else f.about += c + "\n";
                code += "\n";
                continue;
            }
            if (startsWith(c, "@")) {
                std::string tag = c.substr(1, c.find(' ') == std::string::npos ? std::string::npos : c.find(' ') - 1);
                std::string arg = c.find(' ') == std::string::npos ? "" : trim(c.substr(c.find(' ') + 1));
                if (tag == "name") f.name = arg;
                else if (tag == "category") f.category = arg;
                else if (tag == "credit") f.credit = arg;
                else if (tag == "about") inAbout = true;
                else if (tag == "render") parseRenderHints(arg, f.hints);
                else if (tag == "look") {
                    // values may be quoted to contain spaces: palette="Ultra Fractal"
                    size_t i = 0;
                    while (i < arg.size()) {
                        while (i < arg.size() && arg[i] == ' ') i++;
                        size_t e = arg.find('=', i);
                        if (e == std::string::npos) break;
                        std::string k = arg.substr(i, e - i), v;
                        i = e + 1;
                        if (i < arg.size() && arg[i] == '"') {
                            size_t q = arg.find('"', i + 1);
                            v = arg.substr(i + 1, q == std::string::npos ? std::string::npos : q - i - 1);
                            i = q == std::string::npos ? arg.size() : q + 1;
                        } else {
                            size_t sp = arg.find(' ', i);
                            v = arg.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
                            i = sp == std::string::npos ? arg.size() : sp;
                        }
                        f.look.push_back({k, v});
                    }
                }
                else if (tag == "autopilot") {  // around|through [CLEARANCE] [look=X] [orbit=R], in any order after the style
                    std::istringstream as(arg);
                    std::string tok;
                    bool first = true;
                    while (as >> tok) {
                        char* end = nullptr;
                        if (first) {
                            f.autopilotStyle = tok == "through" ? 1 : 0;
                            first = false;
                        } else if (tok.rfind("orbit=", 0) == 0) {
                            float o = std::strtof(tok.c_str() + 6, nullptr);
                            if (std::isfinite(o)) f.autopilotOrbit = std::clamp(o, 0.0f, 10.0f);
                        } else if (tok.rfind("look=", 0) == 0) {
                            float l = std::strtof(tok.c_str() + 5, nullptr);
                            if (std::isfinite(l)) f.autopilotLook = std::clamp(l, 0.0f, 5.0f);
                        } else if (float c = std::strtof(tok.c_str(), &end); end != tok.c_str() && std::isfinite(c) && c > 0) {
                            f.autopilotClearance = std::min(c, 10.0f);
                        } else {
                            f.parseError += path.filename().string() + ":" + std::to_string(lineNo) + ": @autopilot: unknown \"" + tok + "\"\n";
                        }
                    }
                }
                else if (tag == "camera") {
                    std::istringstream cs(arg);
                    cs >> f.camPos[0] >> f.camPos[1] >> f.camPos[2] >> f.camTarget[0] >> f.camTarget[1] >> f.camTarget[2];
                } else if (tag == "param") {
                    Param p;
                    std::string err;
                    if (parseParam(arg, p, err)) f.params.push_back(p);
                    else f.parseError += path.filename().string() + ":" + std::to_string(lineNo) + ": " + err + "\n";
                }
                code += "\n";  // keep line numbers aligned for error messages
                continue;
            }
        }
        code += line + "\n";
    }
    f.code = code;
    return true;
}

void FractalLibrary::load(const fs::path& dir) {
    dir_ = dir;
    fractals_.clear();
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".glsl") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    // Put a pleasant canonical order first; anything else follows alphabetically.
    const char* order[] = {"mandelbulb", "mandelbox", "menger", "sierpinski", "kifs",
                           "quatjulia", "apollonian", "kleinian", "landscape"};
    std::vector<fs::path> sorted;
    for (auto* k : order)
        for (auto& p : files)
            if (p.stem() == k) sorted.push_back(p);
    for (auto& p : files)
        if (std::find(sorted.begin(), sorted.end(), p) == sorted.end()) sorted.push_back(p);
    for (auto& p : sorted) {
        Fractal f;
        if (parseFractalFile(p, f)) fractals_.push_back(std::move(f));
        else fprintf(stderr, "fract3d: %s\n", f.parseError.c_str());
    }
}

std::vector<int> FractalLibrary::reloadChanged() {
    std::vector<int> changed;
    for (int i = 0; i < (int)fractals_.size(); i++) {
        auto& f = fractals_[i];
        std::error_code ec;
        auto t = fs::last_write_time(f.path, ec);
        if (ec || t == f.mtime) continue;
        Fractal nf;
        if (!parseFractalFile(f.path, nf)) continue;
        for (auto& p : nf.params)
            if (Param* old = f.find(p.id); old && old->type == p.type)
                for (int k = 0; k < 4; k++) p.value[k] = old->value[k], p.exact[k] = old->exact[k];
        f = std::move(nf);
        changed.push_back(i);
    }
    return changed;
}

int FractalLibrary::indexOf(const std::string& key) const {
    for (int i = 0; i < (int)fractals_.size(); i++)
        if (fractals_[i].key == key) return i;
    return -1;
}
