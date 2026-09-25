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

std::vector<Palette> builtinPalettes();
Palette makeCosinePalette(const std::string& name, const CosinePalette& cp);
Palette vgaDefaultPalette();

// Palette files: plain text, one "r g b" (0-255) per line, like Fractint .MAP files.
bool loadMapFile(const std::string& path, Palette& out);
bool saveMapFile(const std::string& path, const Palette& p);
