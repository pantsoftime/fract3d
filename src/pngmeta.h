#pragma once
// PNG files that remember how they were made: the view's PAR text is stored in
// an iTXt chunk (keyword "fract3d-par"), so any screenshot or render can be
// dropped back onto the window to reopen the exact view.
#include <cstdint>
#include <string>

bool writePngWithText(const std::string& path, int w, int h, const uint8_t* rgbaBottomUp, const std::string& parText);
// Returns the embedded PAR text, or "" if the file has none (or isn't a PNG).
std::string readPngText(const std::string& path);
