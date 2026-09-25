// Unit tests for importing Fractint .PAR files (no GPU needed).
#include "fractint_par.h"
#include "gl_util.h"

#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static bool has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

int main(int argc, char** argv) {
    bool ok = false;
    std::string text = readTextFile(argc > 1 ? argv[1] : "tests/fractint/sample.par", &ok);
    CHECK(ok, "sample file readable");
    auto entries = parseFractintPar(text);
    CHECK(entries.size() == 5, "five entries");
    if (entries.size() < 5) return 1;
    CHECK(entries[0].name == "Blue_Classic" && entries[0].comment == "the default Mandelbrot, blue inside", "name and comment");
    CHECK(entries[1].keys["maxiter"] == "512", "keys");

    // palette encoding: three base-64 digits per color, <n> shades between neighbours, \ continues lines
    auto pal = decodeFractintColors(entries[1].keys["colors"]);
    CHECK(pal.size() == 256 * 3, "256 colors");
    if (pal.size() == 256 * 3) {
        CHECK(pal[0] == 0 && pal[1] == 0 && pal[2] == 0, "first color black");
        CHECK(pal[31 * 3] == 1.0f && pal[31 * 3 + 1] == 0 && pal[31 * 3 + 2] == 0, "z00 after 30 shades is red at entry 31");
        CHECK(std::abs(pal[15 * 3] - 15.0f / 31.0f) < 1e-5f, "shades are linear");
        CHECK(pal[254 * 3] == 0 && pal[255 * 3] == 1.0f, "the continued (\\) line supplies the last color");
    }
    CHECK(decodeFractintColors("@default.map").empty(), "palette files aren't decoded");

    auto blue = convertFractintEntry(entries[0]);
    CHECK(blue.ok && has(blue.par, "classic.insideMode = 2") && has(blue.par, "classic.insideColor = 0 0 0.666"),
          "default inside=1: VGA blue");
    CHECK(has(blue.par, "classic.center = -0.5 0") && has(blue.par, "classic.height = 3") && has(blue.par, "classic.maxIter = 150"),
          "Fractint's default view and iterations");

    auto sea = convertFractintEntry(entries[1]);
    CHECK(sea.ok && has(sea.par, "classic.center = -0.743566") && has(sea.par, " 0.131402"), "center-mag center");
    CHECK(has(sea.par, "classic.height = 0.0013333333333333333"), "height = 2 / mag");
    CHECK(has(sea.par, "color.palette = Custom (gradient editor)") && has(sea.par, " 0.12109375 1 0 0;"), "palette as 256 stops");
    CHECK(has(sea.par, "classic.insideMode = 0"), "inside=0 is black");

    auto jul = convertFractintEntry(entries[2]);
    CHECK(jul.ok && has(jul.par, "classic.julia = 1") && has(jul.par, "classic.juliaC = -0.80000000000000004 0.156"), "julia c from params");
    CHECK(has(jul.par, "classic.center = 0 0") && has(jul.par, "classic.height = 2.3999999999999999"), "corners");
    CHECK(has(jul.par, "classic.insideMode = 1"), "inside=zmag");

    auto frm = convertFractintEntry(entries[3]);
    CHECK(frm.ok && has(frm.par, "classic.formula = 8") && has(frm.par, "formula.name = Spider"), "formula entries");
    CHECK(has(frm.par, "classic.p1 = 0.10000000000000001 0.20000000000000001"), "formula params become p1");
    CHECK(has(frm.par, "classic.coloring = 1"), "decomp=2 is binary decomposition");
    bool rotWarn = false, fileWarn = false;
    for (auto& w : frm.warnings) rotWarn |= has(w, "rotation"), fileWarn |= has(w, "fractint.frm");
    CHECK(rotWarn && fileWarn, "rotation and the formula file are reported");

    auto bad = convertFractintEntry(entries[4]);
    CHECK(!bad.ok && has(bad.error, "lsystem"), "unsupported types are reported");

    CHECK(parseFractintPar("mode = 2d\nclassic.maxIter = 5\n").empty(), "a Fract3D PAR isn't a Fractint one");

    printf("%s (%d failure%s)\n", failures ? "FAILED" : "all Fractint import tests passed", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
