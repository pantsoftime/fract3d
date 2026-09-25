#include "palettes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

static void put(Palette& p, int i, float r, float g, float b) {
    p.rgba[i * 4 + 0] = (uint8_t)std::clamp((int)std::lround(r * 255.0f), 0, 255);
    p.rgba[i * 4 + 1] = (uint8_t)std::clamp((int)std::lround(g * 255.0f), 0, 255);
    p.rgba[i * 4 + 2] = (uint8_t)std::clamp((int)std::lround(b * 255.0f), 0, 255);
    p.rgba[i * 4 + 3] = 255;
}

static Palette blank(const std::string& name) {
    Palette p;
    p.name = name;
    p.rgba.assign(256 * 4, 255);
    return p;
}

// The IBM VGA mode 13h power-on palette, which is what Fractint used unless you
// loaded a .MAP file: 16 EGA colors, 16 grays, then 9 rings of 24 hues
// (3 intensities x 3 saturations), then 8 blacks. Values are 6-bit DAC levels.
Palette vgaDefaultPalette() {
    Palette p = blank("Fractint VGA (default)");
    static const int ega[16][3] = {{0, 0, 0},   {0, 0, 42},   {0, 42, 0},   {0, 42, 42},
                                   {42, 0, 0},  {42, 0, 42},  {42, 21, 0},  {42, 42, 42},
                                   {21, 21, 21}, {21, 21, 63}, {21, 63, 21}, {21, 63, 63},
                                   {63, 21, 21}, {63, 21, 63}, {63, 63, 21}, {63, 63, 63}};
    for (int i = 0; i < 16; i++) put(p, i, ega[i][0] / 63.f, ega[i][1] / 63.f, ega[i][2] / 63.f);
    static const int gray[16] = {0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63};
    for (int i = 0; i < 16; i++) put(p, 16 + i, gray[i] / 63.f, gray[i] / 63.f, gray[i] / 63.f);
    // five levels per ring: lo, 1/4, 1/2, 3/4, hi
    static const int rings[9][5] = {{0, 16, 31, 47, 63},  {31, 39, 47, 55, 63}, {45, 49, 54, 58, 63},
                                    {0, 7, 14, 21, 28},   {14, 17, 21, 24, 28}, {20, 22, 24, 26, 28},
                                    {0, 4, 8, 12, 16},    {8, 10, 12, 14, 16},  {11, 12, 13, 15, 16}};
    int idx = 32;
    for (auto& L : rings) {
        // hue walk: blue -> magenta -> red -> yellow -> green -> cyan -> (blue)
        int seq[24][3];
        for (int k = 0; k < 4; k++) {
            seq[k][0] = L[k];      seq[k][1] = L[0];      seq[k][2] = L[4];      // B..M: r rises
            seq[4 + k][0] = L[4];  seq[4 + k][1] = L[0];  seq[4 + k][2] = L[4 - k]; // M..R: b falls
            seq[8 + k][0] = L[4];  seq[8 + k][1] = L[k];  seq[8 + k][2] = L[0];  // R..Y: g rises
            seq[12 + k][0] = L[4 - k]; seq[12 + k][1] = L[4]; seq[12 + k][2] = L[0]; // Y..G: r falls
            seq[16 + k][0] = L[0]; seq[16 + k][1] = L[4]; seq[16 + k][2] = L[k];  // G..C: b rises
            seq[20 + k][0] = L[0]; seq[20 + k][1] = L[4 - k]; seq[20 + k][2] = L[4]; // C..B: g falls
        }
        for (auto& c : seq) put(p, idx++, c[0] / 63.f, c[1] / 63.f, c[2] / 63.f);
    }
    for (; idx < 256; idx++) put(p, idx, 0, 0, 0);
    return p;
}

Palette makeCosinePalette(const std::string& name, const CosinePalette& cp) {
    Palette p = blank(name);
    for (int i = 0; i < 256; i++) {
        float t = i / 256.0f, rgb[3];
        for (int k = 0; k < 3; k++)
            rgb[k] = cp.a[k] + cp.b[k] * std::cos(6.2831853f * (cp.c[k] * t + cp.d[k]));
        put(p, i, rgb[0], rgb[1], rgb[2]);
    }
    return p;
}

struct Stop { float t; uint32_t hex; };

// Gradient through the stops; the last stop wraps to the first so the palette
// tiles seamlessly when the color index repeats.
Palette makeGradientPalette(const std::string& name, std::vector<GradientStop> stops) {
    Palette p = blank(name);
    if (stops.empty()) stops.push_back({0.0f, {0.5f, 0.5f, 0.5f}});
    for (auto& s : stops) s.t = s.t - std::floor(s.t);
    std::sort(stops.begin(), stops.end(), [](const GradientStop& a, const GradientStop& b) { return a.t < b.t; });
    // one copy of the first stop past the end makes the last interval wrap around
    GradientStop wrap = stops.front();
    wrap.t += 1.0f;
    stops.push_back(wrap);
    for (int i = 0; i < 256; i++) {
        float t = i / 256.0f;
        if (t < stops.front().t) t += 1.0f;  // before the first stop: in the wrapping interval
        size_t k = 0;
        while (k + 2 < stops.size() && t >= stops[k + 1].t) k++;
        const GradientStop &a = stops[k], &b = stops[k + 1];
        float u = std::clamp((t - a.t) / std::max(b.t - a.t, 1e-6f), 0.0f, 1.0f);
        u = u * u * (3 - 2 * u);
        put(p, i, a.rgb[0] + (b.rgb[0] - a.rgb[0]) * u, a.rgb[1] + (b.rgb[1] - a.rgb[1]) * u,
            a.rgb[2] + (b.rgb[2] - a.rgb[2]) * u);
    }
    return p;
}

std::vector<GradientStop> sampleStops(const Palette& p, int n) {
    std::vector<GradientStop> out;
    for (int i = 0; i < n; i++) {
        int k = i * 256 / n;
        out.push_back({(float)i / n, {p.rgba[k * 4] / 255.0f, p.rgba[k * 4 + 1] / 255.0f, p.rgba[k * 4 + 2] / 255.0f}});
    }
    return out;
}

static Palette gradient(const std::string& name, const std::vector<Stop>& hexStops) {
    std::vector<GradientStop> stops;
    auto ch = [](uint32_t h, int s) { return ((h >> s) & 0xff) / 255.0f; };
    for (auto& s : hexStops) stops.push_back({s.t, {ch(s.hex, 16), ch(s.hex, 8), ch(s.hex, 0)}});
    return makeGradientPalette(name, stops);
}

std::vector<Palette> builtinPalettes() {
    std::vector<Palette> v;
    v.push_back(gradient("Ultra Fractal", {{0.0f, 0x000764}, {0.16f, 0x206bcb}, {0.42f, 0xedffff},
                                           {0.6425f, 0xffaa00}, {0.8575f, 0x000200}}));
    v.push_back(vgaDefaultPalette());
    {
        Palette ega = blank("Fractint EGA 16");
        Palette vga = vgaDefaultPalette();
        for (int i = 0; i < 256; i++)
            for (int k = 0; k < 4; k++) ega.rgba[i * 4 + k] = vga.rgba[(i % 16) * 4 + k];
        v.push_back(ega);
    }
    v.push_back(gradient("Plaster", {{0.0f, 0xf2eee6}, {0.5f, 0xd9d4ca}}));
    v.push_back(gradient("Gold", {{0.0f, 0x2a1606}, {0.3f, 0x9a6214}, {0.55f, 0xf5c84c}, {0.75f, 0xfff4cf}}));
    v.push_back(gradient("Fire", {{0.0f, 0x100000}, {0.25f, 0xa01000}, {0.5f, 0xff7a00}, {0.7f, 0xffe060}, {0.85f, 0xfff8e0}}));
    v.push_back(gradient("Ice", {{0.0f, 0x03102a}, {0.3f, 0x1b5aa0}, {0.6f, 0x7fd8f0}, {0.8f, 0xf0fbff}}));
    v.push_back(gradient("Twilight", {{0.0f, 0x1b0c3a}, {0.25f, 0x5a2a82}, {0.5f, 0xd9607a}, {0.75f, 0xf6c28b}}));
    v.push_back(gradient("Forest", {{0.0f, 0x16240f}, {0.3f, 0x4a6b2a}, {0.55f, 0xa7b36b}, {0.8f, 0x6b4a2a}}));
    v.push_back(gradient("Neon", {{0.0f, 0x0a0014}, {0.25f, 0xff2bd6}, {0.5f, 0x2b0a3d}, {0.75f, 0x19f0ff}}));
    v.push_back(gradient("Copper Patina", {{0.0f, 0x3b1d0e}, {0.3f, 0xb86b3a}, {0.55f, 0xe8b27a}, {0.8f, 0x4f9e8a}}));
    CosinePalette rainbow;
    v.push_back(makeCosinePalette("Rainbow (cosine)", rainbow));
    CosinePalette psy{{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {2.0f, 1.0f, 0.0f}, {0.5f, 0.2f, 0.25f}};
    v.push_back(makeCosinePalette("Psychedelic (cosine)", psy));
    CosinePalette dusk{{0.8f, 0.5f, 0.4f}, {0.2f, 0.4f, 0.2f}, {2.0f, 1.0f, 1.0f}, {0.0f, 0.25f, 0.25f}};
    v.push_back(makeCosinePalette("Dusk (cosine)", dusk));
    return v;
}

bool loadMapFile(const std::string& path, Palette& out) {
    std::ifstream f(path);
    if (!f) return false;
    out = blank(std::filesystem::path(path).stem().string());
    std::string line;
    int i = 0;
    while (i < 256 && std::getline(f, line)) {
        std::istringstream is(line);
        int r, g, b;
        if (!(is >> r >> g >> b)) continue;
        put(out, i++, r / 255.f, g / 255.f, b / 255.f);
    }
    // Fractint maps with fewer than 256 entries repeat
    for (int k = i; k < 256 && i > 0; k++)
        for (int c = 0; c < 4; c++) out.rgba[k * 4 + c] = out.rgba[(k % i) * 4 + c];
    return i > 0;
}

bool saveMapFile(const std::string& path, const Palette& p) {
    std::ofstream f(path);
    if (!f) return false;
    for (int i = 0; i < 256; i++)
        f << (int)p.rgba[i * 4] << ' ' << (int)p.rgba[i * 4 + 1] << ' ' << (int)p.rgba[i * 4 + 2] << '\n';
    return true;
}
