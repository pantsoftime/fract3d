#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Palette {
    std::string name;
    std::vector<uint8_t> rgba;  // 256 entries * 4
};

struct CosinePalette {
    // color(t) = a + b * cos(2*pi*(c*t + d))  — Inigo Quilez's procedural palette
    float a[3] = {0.5f, 0.5f, 0.5f}, b[3] = {0.5f, 0.5f, 0.5f};
    float c[3] = {1.0f, 1.0f, 1.0f}, d[3] = {0.0f, 0.33f, 0.67f};
};

// A color at a position 0..1 along the palette. The gradient wraps around from
// the last stop back to the first, so palettes tile seamlessly.
struct GradientStop {
    float t;
    float rgb[3];
};
Palette makeGradientPalette(const std::string& name, std::vector<GradientStop> stops);
// Samples n evenly spaced stops from any palette (to start editing from it).
std::vector<GradientStop> sampleStops(const Palette& p, int n);

std::vector<Palette> builtinPalettes();
Palette makeCosinePalette(const std::string& name, const CosinePalette& cp);
Palette vgaDefaultPalette();

// Palette files: plain text, one "r g b" (0-255) per line, like Fractint .MAP files.
bool loadMapFile(const std::string& path, Palette& out);
bool saveMapFile(const std::string& path, const Palette& p);
