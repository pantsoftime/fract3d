// The "@autopilot" hint in fractal files (src/fractal_lib.cpp): the style, an optional
// clearance and optional look=/orbit=/inside= in any order after it; unknown tokens are reported.
// Also parses every fractal that ships, which must have no errors.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "fractal_lib.h"

namespace fs = std::filesystem;
static int failures = 0;
static void check(bool ok, const char* what) {
    printf("%-60s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}
static bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

static Fractal parse(const std::string& hint) {
    fs::path p = fs::temp_directory_path() / "fract3d-hint-test.glsl";
    {
        std::ofstream o(p);
        o << "// @name Test\n// @camera 0 0 -3   0 0 0\n" << hint << "\nfloat DE(vec3 p, inout vec4 trap) { return length(p) - 1.0; }\n";
    }
    Fractal f;
    parseFractalFile(p, f);
    fs::remove(p);
    return f;
}

int main(int argc, char** argv) {
    Fractal d = parse("");
    check(d.autopilotStyle == 0 && near(d.autopilotClearance, 0.25f) && near(d.autopilotLook, 1.2f) && d.autopilotOrbit == 0,
          "no hint: around, the default clearance and gaze, no orbit");
    Fractal a = parse("// @autopilot through 0.03");
    check(a.autopilotStyle == 1 && near(a.autopilotClearance, 0.03f), "through with a clearance");
    Fractal b = parse("// @autopilot around 0.08 look=0.35 orbit=0.35");
    check(b.autopilotStyle == 0 && near(b.autopilotClearance, 0.08f) && near(b.autopilotLook, 0.35f) && near(b.autopilotOrbit, 0.35f),
          "clearance, look= and orbit=");
    Fractal c = parse("// @autopilot around look=0.5 orbit=0.2");
    check(near(c.autopilotClearance, 0.25f) && near(c.autopilotLook, 0.5f) && near(c.autopilotOrbit, 0.2f),
          "no clearance: the options after the style still apply");
    Fractal e = parse("// @autopilot through orbit=0.3 0.07 look=2");
    check(e.autopilotStyle == 1 && near(e.autopilotClearance, 0.07f) && near(e.autopilotOrbit, 0.3f) && near(e.autopilotLook, 2.0f),
          "any order after the style");
    Fractal g = parse("// @autopilot around 1e9 look=99 orbit=-4");
    check(near(g.autopilotClearance, 10.0f) && near(g.autopilotLook, 5.0f) && near(g.autopilotOrbit, 0.0f), "out-of-range values are clamped");
    Fractal h = parse("// @autopilot around -1 nan zoom=3");
    check(near(h.autopilotClearance, 0.25f) && h.parseError.find("zoom=3") != std::string::npos, "bad values are ignored, unknown words reported");
    check(h.parseError.find("-1") != std::string::npos && h.parseError.find("nan") != std::string::npos, "a negative or NaN clearance is reported");
    Fractal m = parse("// @autopilot through 0.03 inside=0.001");
    check(m.autopilotStyle == 1 && near(m.autopilotClearance, 0.03f) && near(m.autopilotInside, 0.001f) && d.autopilotInside == 0,
          "inside= (none unless given)");

    if (argc > 1) {  // every fractal that ships parses without errors
        int n = 0, bad = 0;
        for (auto& ent : fs::directory_iterator(argv[1])) {
            if (ent.path().extension() != ".glsl") continue;
            Fractal f;
            n++;
            if (!parseFractalFile(ent.path(), f) || !f.parseError.empty()) {
                bad++;
                printf("  %s: %s\n", ent.path().filename().c_str(), f.parseError.c_str());
            }
        }
        check(n > 0 && bad == 0, "every shipped fractal file parses cleanly");
    }
    printf("%s\n", failures ? "fractal hint test FAILED" : "fractal hint test passed");
    return failures ? 1 : 0;
}
