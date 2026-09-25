// Unit tests for the formula parser/transpiler (no GPU needed).
#include "formula.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int failures = 0;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
            failures++;                                               \
        }                                                             \
    } while (0)

static TranspiledFormula tr(const std::string& src) {
    int fn[4] = {0, 0, 0, 0};
    return transpileFormula(src, fn);
}

static bool errorContains(const std::string& src, const std::string& needle) {
    auto t = tr(src);
    if (t.ok) return false;
    if (t.error.find(needle) == std::string::npos) {
        fprintf(stderr, "  error was: %s\n", t.error.c_str());
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    // every shipped formula must transpile
    std::ifstream f(argc > 1 ? argv[1] : "formulas/classics.frm");
    std::stringstream ss;
    ss << f.rdbuf();
    auto defs = parseFormulaFile(ss.str());
    CHECK(defs.size() >= 10, "classics.frm should contain the shipped formulas");
    for (auto& d : defs) {
        auto t = tr(d.source);
        if (!t.ok) fprintf(stderr, "  %s: %s\n", d.name.c_str(), t.error.c_str());
        CHECK(t.ok, ("shipped formula compiles: " + d.name).c_str());
    }

    // annotations and comments
    for (auto& d : defs)
        if (d.name == "Julia") {
            CHECK(d.hasP[0] && d.p[0][0] == -0.8f && d.p[0][1] == 0.156f, "@p1 annotation parsed");
            CHECK(d.hasView && d.view[2] == 2.6, "@view annotation parsed");
            CHECK(d.comment.find("@") == std::string::npos, "annotations are not part of the description");
        }

    // syntax Fractint users will type
    CHECK(tr("M { z = 0, c = pixel: z = z*z + c, |z| <= 4 }").ok, "one-line formula");
    CHECK(tr("M(XAXIS) { z = c = pixel:\n z = sqr(z) + c\n |z| <= 4 }").ok, "symmetry hint, chained assignment, newlines");
    CHECK(tr("M { z = pixel: z = z^(2.5, 0.1) + pixel, |z| < 100 && real(z) > -50 }").ok, "complex literal, power, &&");
    CHECK(tr("M { ; a comment\n z = 0 : z = fn1(z) + pixel ; trailing comment\n |z| <= 64 }").usesFn[0], "fn1 detected");
    CHECK(tr("M { z = p1: z = z*z + p2, |z| <= 4 }").usesP[1], "p2 detected");
    CHECK(tr("M { z = 0: z = z*z + pixel\n |z| <= 4 }").stateVars == 1, "only z is carried over");
    CHECK(tr("NoInit { z = sqr(z) + pixel, |z| <= 4 }").ok, "formula without an init section");
    CHECK(tr("Case { Z = 0, C = PIXEL: Z = Z*Z + C, |Z| <= 4 }").ok, "case-insensitive like Fractint");

    // smooth coloring: the escape radius comes from the bailout test (|z| is the squared modulus)
    CHECK(tr("M { z = 0: z = z*z + pixel, |z| <= 4 }").bailout == 2.0, "|z| <= 4 means radius 2");
    CHECK(tr("M { z = 0: z = z*z + pixel, cabs(z) < 10 }").bailout == 10.0, "cabs(z) < 10 means radius 10");
    CHECK(tr("M { z = 0: z = exp(z) + pixel, |real(z)| < 50 }").bailout == 0.0, "other tests: radius unknown");
    CHECK(tr("M { z = 0: z = z*z + pixel, |z| <= 4 && |z - 1| > 0.01 }").bailout == 0.0, "compound tests: unknown");
    CHECK(tr("M { ; @power = 3\n z = 0: z = z*z*z + pixel, |z| <= 4 }").power == 3.0, "@power annotation");
    CHECK(tr("M { ; @bailout = 8\n z = 0: z = exp(z) + pixel, |real(z)| < 50 }").bailout == 8.0, "@bailout annotation");
    CHECK(tr("M { ; @smooth = 0\n z = 0: z = z*z + pixel, |z| <= 4 }").bailout == 0.0, "@smooth = 0");

    // helpful errors
    CHECK(errorContains("M { z = 0: z = foo(z), |z| < 4 }", "unknown function 'foo'"), "unknown function");
    CHECK(errorContains("M { z = 0: z = w*z, |z| < 4 }", "'w' is used but never assigned"), "unassigned variable");
    CHECK(errorContains("M { pixel = 0: z = z, |z| < 4 }", "read-only"), "assigning pixel");
    CHECK(errorContains("M { z = 0: z = z*z + pixel, |z| < 4", "missing '}'"), "missing brace");
    CHECK(errorContains("M { z = 0: z = (z + 1, |z| < 4 }", "expected ')'"), "unbalanced parenthesis");
    CHECK(errorContains("M { z = 0: z = z $ 2, |z| < 4 }", "unexpected character"), "bad character");
    CHECK(errorContains("M { a=0,b=0,c=0,d=0,e2=0,f=0,g=0: z = a+b+c+d+e2+f+g, |z|<4 }", "at most 6"), "too many variables");
    CHECK(errorContains("M {\n z = 0 :\n z = z*z +\n |z| < 4 }", "line 3: the statement ends too early"), "errors report the line");

    printf("%s (%d failure%s)\n", failures ? "FAILED" : "all formula tests passed", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
