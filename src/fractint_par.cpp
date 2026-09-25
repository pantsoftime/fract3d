#include "fractint_par.h"
#include "palettes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// "a/b/c" -> numbers (Fractint separates list values with slashes)
std::vector<double> numbers(const std::string& v) {
    std::vector<double> out;
    std::stringstream ss(v);
    std::string item;
    while (std::getline(ss, item, '/')) {
        char* end = nullptr;
        double d = strtod(item.c_str(), &end);
        out.push_back(end != item.c_str() && std::isfinite(d) ? d : 0.0);
    }
    return out;
}

int digit64(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c == '_' || c == '`') return c - '_' + 36;
    if (c >= 'a' && c <= 'z') return c - 'a' + 38;
    return -1;
}

std::string fmt(double v) {
    char b[40];
    snprintf(b, sizeof b, "%.17g", v);
    return b;
}

}  // namespace

std::vector<float> decodeFractintColors(const std::string& v) {
    if (v.empty() || v[0] == '@') return {};
    std::vector<float> out;  // r, g, b per entry
    int pendingShade = 0;
    for (size_t i = 0; i < v.size() && out.size() < 256 * 3;) {
        if (v[i] == '<') {
            size_t close = v.find('>', i);
            if (close == std::string::npos) return {};
            pendingShade = atoi(v.c_str() + i + 1);
            i = close + 1;
            continue;
        }
        if (i + 3 > v.size()) return {};
        int c[3];
        for (int k = 0; k < 3; k++)
            if ((c[k] = digit64(v[i + k])) < 0) return {};
        i += 3;
        float rgb[3] = {c[0] / 63.0f, c[1] / 63.0f, c[2] / 63.0f};
        if (pendingShade > 0 && out.size() >= 3) {  // shade from the previous color to this one
            float prev[3] = {out[out.size() - 3], out[out.size() - 2], out[out.size() - 1]};
            for (int s = 1; s <= pendingShade && out.size() < 256 * 3; s++)
                for (int k = 0; k < 3; k++) out.push_back(prev[k] + (rgb[k] - prev[k]) * s / (pendingShade + 1));
        }
        pendingShade = 0;
        for (int k = 0; k < 3 && out.size() < 256 * 3; k++) out.push_back(rgb[k]);
    }
    if (out.empty()) return {};
    while (out.size() < 256 * 3) out.push_back(0.0f);  // short palettes: the rest is black, as in Fractint
    return out;
}

std::vector<FractintEntry> parseFractintPar(const std::string& text) {
    std::vector<FractintEntry> out;
    // join "\"-continued lines, drop comments (but keep the first one of each entry)
    std::istringstream is(text);
    std::string line, joined;
    std::vector<std::pair<std::string, std::string>> lines;  // (code, comment)
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!joined.empty()) line.erase(0, line.find_first_not_of(" \t"));  // continuation lines are indented
        if (!line.empty() && line.back() == '\\') {
            joined += line.substr(0, line.size() - 1);
            continue;
        }
        joined += line;
        size_t semi = joined.find(';');
        lines.push_back({semi == std::string::npos ? joined : joined.substr(0, semi),
                         semi == std::string::npos ? "" : joined.substr(semi + 1)});
        joined.clear();
    }
    FractintEntry cur;
    bool in = false;
    for (auto& [code, comment] : lines) {
        std::string c = code;
        if (!in) {
            size_t brace = c.find('{');
            if (brace == std::string::npos) continue;
            std::string name = c.substr(0, brace);
            name.erase(0, name.find_first_not_of(" \t"));
            while (!name.empty() && isspace((unsigned char)name.back())) name.pop_back();
            if (name.empty() || name.find('=') != std::string::npos) continue;
            cur = FractintEntry();
            cur.name = name;
            in = true;
            c = c.substr(brace + 1);
        }
        if (in && cur.comment.empty()) {
            std::string t = comment;
            t.erase(0, t.find_first_not_of(" \t;"));
            cur.comment = t;
        }
        size_t close = c.find('}');
        std::istringstream ts(close == std::string::npos ? c : c.substr(0, close));
        std::string tok;
        while (ts >> tok) {
            size_t eq = tok.find('=');
            if (eq == std::string::npos || eq == 0) continue;
            cur.keys[lower(tok.substr(0, eq))] = tok.substr(eq + 1);
        }
        if (close != std::string::npos) {
            if (cur.keys.count("type") || cur.keys.count("center-mag") || cur.keys.count("corners")) out.push_back(cur);
            in = false;
        }
    }
    return out;
}

FractintImport convertFractintEntry(const FractintEntry& e) {
    FractintImport r;
    auto get = [&](const char* k) {
        auto it = e.keys.find(k);
        return it == e.keys.end() ? std::string() : it->second;
    };
    auto warn = [&](const std::string& w) { r.warnings.push_back(w); };
    std::vector<double> params = numbers(get("params"));
    auto param = [&](size_t i) { return i < params.size() ? params[i] : 0.0; };

    std::string type = lower(get("type"));
    if (type.empty()) type = "mandel";
    if (type.size() > 2 && type.compare(type.size() - 2, 2, "fp") == 0 && type != "formula") type.resize(type.size() - 2);
    std::ostringstream o;
    o << "; " << e.name << (e.comment.empty() ? "" : " - " + e.comment) << " (imported from a Fractint PAR)\n";
    o << "mode = 2d\n";
    bool julia = false;
    int formula = 0;
    double defCx = -0.5, defH = 3.0;  // Fractint's default Mandelbrot corners: -2.5..1.5 x -1.5..1.5
    if (type == "mandel") {
        formula = 0;
    } else if (type == "julia") {
        julia = true;
    } else if (type == "mandel4" || type == "julia4") {
        formula = 3;
        julia = type == "julia4";
        o << "classic.power = 4\n";
    } else if (type == "newton" || type == "newtbasin") {
        formula = 4;
        if (params.size() && (int)param(0) != 3) warn("Newton's method of degree " + fmt(param(0)) + " is drawn with degree 3");
        defCx = 0;
    } else if (type == "lambda" || type == "mandellambda") {
        formula = 6;
        julia = type == "lambda";
    } else if (type == "magnet1m" || type == "magnet1j") {
        formula = 7;
        julia = type == "magnet1j";
        defCx = 1.5, defH = 8;
    } else if (type == "phoenix") {
        // Fractint: z' = z^2 + p1 + p2 y, y' = z  (a Julia-type set)
        formula = 5;
        julia = true;
        o << "classic.juliaC = " << fmt(param(0)) << " 0\n";
        o << "classic.phoenixP = " << fmt(param(1)) << " 0\n";
        if (param(2) != 0 && (int)param(2) != 2) warn("Phoenix of degree " + fmt(param(2)) + " is drawn with degree 2");
    } else if (type == "manowar") {
        formula = 8;
        o << "formula.name = Manowar\n";
    } else if (type == "formula") {
        formula = 8;
        std::string name = get("formulaname");
        if (name.empty()) {
            r.error = "the entry uses a formula but doesn't say which (no formulaname=)";
            return r;
        }
        o << "formula.name = " << name << "\n";
        const char* ps[5] = {"classic.p1", "classic.p2", "classic.p3", "classic.p4", "classic.p5"};
        for (size_t i = 0; i < 5 && 2 * i < params.size(); i++) o << ps[i] << " = " << fmt(param(2 * i)) << " " << fmt(param(2 * i + 1)) << "\n";
        if (!get("formulafile").empty())
            warn("the formula comes from " + get("formulafile") + ": it must be in your formula files (or drop the .frm onto the window first)");
    } else {
        r.error = "Fractint type '" + type + "' isn't supported (supported: mandel, julia, mandel4, julia4, newton, lambda, "
                  "mandellambda, magnet1m, magnet1j, phoenix, manowar, formula)";
        return r;
    }
    if (julia && formula != 5) o << "classic.juliaC = " << fmt(param(0)) << " " << fmt(param(1)) << "\n";
    if (julia) defCx = 0;
    o << "classic.formula = " << formula << "\n";
    o << "classic.julia = " << (julia ? 1 : 0) << "\n";

    // the view: center-mag=X/Y/Mag[/Xmagfactor/Rotation/Skew] (the visible height is 2/Mag) or corners=
    std::vector<double> cm = numbers(get("center-mag")), co = numbers(get("corners"));
    std::string cmRaw = get("center-mag");
    if (cm.size() >= 3 && cm[2] > 0) {
        // keep every digit Fractint wrote: the center goes through classic.centerHP
        std::vector<std::string> parts;
        std::stringstream ss(cmRaw);
        std::string item;
        while (std::getline(ss, item, '/')) parts.push_back(item);
        o << "classic.center = " << fmt(cm[0]) << " " << fmt(cm[1]) << "\n";
        if (parts.size() >= 2 && parts[0].size() > 17) o << "classic.centerHP = " << parts[0] << " " << parts[1] << "\n";
        o << "classic.height = " << fmt(2.0 / cm[2]) << "\n";
        if (cm.size() >= 4 && std::abs(cm[3] - 1.0) > 1e-6) warn("the stretched aspect ratio (x magnification " + fmt(cm[3]) + ") is drawn unstretched");
        if (cm.size() >= 5 && std::abs(cm[4]) > 1e-9) warn("the view's " + fmt(cm[4]) + " degree rotation is left out");
        if (cm.size() >= 6 && std::abs(cm[5]) > 1e-9) warn("the view's skew is left out");
    } else if (co.size() >= 4) {
        o << "classic.center = " << fmt((co[0] + co[1]) / 2) << " " << fmt((co[2] + co[3]) / 2) << "\n";
        o << "classic.height = " << fmt(std::abs(co[3] - co[2])) << "\n";
        if (co.size() >= 6) warn("the rotated/skewed corners are drawn as a plain rectangle");
    } else {
        o << "classic.center = " << fmt(defCx) << " 0\n";
        o << "classic.height = " << fmt(defH) << "\n";
    }

    int maxiter = get("maxiter").empty() ? 150 : atoi(get("maxiter").c_str());  // Fractint's default
    o << "classic.maxIter = " << std::max(maxiter, 2) << "\n";
    if (!get("bailout").empty()) o << "classic.bailout = " << fmt(std::sqrt(std::max(atof(get("bailout").c_str()), 4.0))) << "\n";  // Fractint compares |z|^2
    std::string test = lower(get("bailoutest"));
    if (!test.empty() && test != "mod") warn("the '" + test + "' bailout test is drawn with the usual |z| test");
    o << "classic.banded = 1\nclassic.colorDensity = 1\nclassic.supersample = 1\nclassic.fp64 = 2\n";  // Fractint's look: one color per iteration

    // coloring
    std::vector<float> pal = decodeFractintColors(get("colors"));
    std::string inside = lower(get("inside"));
    if (inside.empty()) inside = "1";  // Fractint's default: color 1, the famous blue inside of the Mandelbrot set
    if (inside == "zmag") {
        o << "classic.insideMode = 1\n";
    } else if (isdigit((unsigned char)inside[0])) {
        int idx = std::clamp(atoi(inside.c_str()), 0, 255);
        if (idx == 0) {
            o << "classic.insideMode = 0\n";
        } else {  // a palette color: from the PAR's own colors, or the VGA default palette
            float rgb[3];
            if (!pal.empty()) {
                std::copy(&pal[idx * 3], &pal[idx * 3] + 3, rgb);
            } else {
                Palette vga = vgaDefaultPalette();
                for (int k = 0; k < 3; k++) rgb[k] = vga.rgba[idx * 4 + k] / 255.0f;
            }
            o << "classic.insideMode = 2\nclassic.insideColor = " << rgb[0] << " " << rgb[1] << " " << rgb[2] << "\n";
        }
    } else if (inside == "maxiter") {
        o << "classic.insideMode = 0\n";
        warn("inside=maxiter is drawn black");
    } else {
        o << "classic.insideMode = 0\n";
        warn("inside=" + inside + " coloring is drawn black");
    }
    std::string outside = lower(get("outside"));
    int coloring = 0;
    if (outside == "atan") coloring = 2;
    else if (!outside.empty() && outside != "iter") warn("outside=" + outside + " coloring is drawn as escape time");
    if (!get("decomp").empty() && atoi(get("decomp").c_str()) == 2) coloring = 1;
    else if (!get("decomp").empty()) warn("decomp=" + get("decomp") + " is drawn as escape time (only decomp=2 is supported)");
    if (!get("biomorph").empty() && get("biomorph") != "-1") coloring = 6;
    o << "classic.coloring = " << coloring << "\n";
    if (lower(get("logmap")) == "yes" || lower(get("logmap")) == "y" || (!get("logmap").empty() && isdigit((unsigned char)get("logmap")[0]) && atoi(get("logmap").c_str()) != 0))
        warn("the logarithmic palette map (logmap) is left out");
    if (!get("potential").empty()) warn("continuous potential coloring is left out");

    // the palette: 256 gradient stops reproduce it exactly
    if (!pal.empty()) {
        o << "color.palette = Custom (gradient editor)\ncolor.gradient =";
        char b[80];
        for (int i = 0; i < 256; i++) {
            snprintf(b, sizeof b, " %.8g %.6g %.6g %.6g;", i / 256.0, pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]);
            o << b;
        }
        o << "\n";
    } else {
        o << "color.palette = Fractint VGA (default)\n";
        if (!get("colors").empty()) warn("the palette comes from the file " + get("colors").substr(1) + ": load it with a .map drop");
    }
    if (!get("3d").empty() && lower(get("3d")) != "no") warn("Fractint's 3D transform is left out (try Lift into 3D)");
    r.par = o.str();
    r.ok = true;
    return r;
}
