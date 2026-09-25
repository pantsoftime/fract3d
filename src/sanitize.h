#pragma once
// Validation for values that come from outside the program (PAR files, prefs,
// command line): replaces non-finite numbers with defaults, clamps ranges and
// keeps every enum index inside its table.
#include "camera.h"
#include "fractal_lib.h"
#include "state.h"

void sanitize(RenderSettings& rs);
void sanitize(Classic2DSettings& cs);
void sanitize(Camera& cam);
void sanitize(Fractal& f);  // parameter values against their declared types/ranges
