// Unit tests for the formula parser/transpiler (no GPU needed).
#include "formula.h"

#include <complex>
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

    // ---- the CPU interpreter (FormulaVM): same semantics as the GLSL
    using C = std::complex<double>;
    auto near = [](C a, C b, double tol = 1e-9) { return std::abs(a - b) <= tol * std::max(1.0, std::abs(b)); };
    auto run = [&](const std::string& src, C pixel, int steps, const int* fn = nullptr, C p1 = 0) {
        int f0[4] = {0, 0, 0, 0};
        auto t = transpileFormula(src, fn ? fn : f0);
        if (!t.ok) fprintf(stderr, "  %s\n", t.error.c_str());
        FormulaVM vm(t);
        C ps[kFormulaParams] = {p1, 0, 0, 0, 0};
        vm.setParams(ps, 100);
        vm.init(pixel);
        for (int i = 0; i < steps; i++) vm.step();
        return vm.z();
    };
    {   // Mandelbrot orbit against plain complex arithmetic
        C c(-0.2, 0.3), z = 0;  // (inside the set: the orbit converges, so rounding differences don't grow)
        for (int i = 0; i < 50; i++) z = z * z + c;
        CHECK(near(run("M { z = 0, c = pixel: z = z*z + c, |z| <= 4 }", c, 50), z), "VM: Mandelbrot orbit");
    }
    {   // escape iteration
        auto t = tr("M { z = 0: z = z*z + pixel, |z| <= 4 }");
        FormulaVM vm(t);
        vm.init(C(0.3, 0.6));
        int n = 0;
        while (vm.step() && n < 1000) n++;
        C z = 0;
        int m = 0;
        for (;; m++) { z = z * z + C(0.3, 0.6); if (std::norm(z) > 4) break; }
        CHECK(n == m, "VM: escapes at the same iteration as direct arithmetic");
    }
    // if / elseif / else / endif (on separate lines, and compact)
    const char* branchy = "B { z = 0 :\n if (real(pixel) < 0)\n z = (-1, 0)\n elseif (real(pixel) < 1)\n z = (1, 0)\n else\n z = (2, 0)\n endif\n 0 }";
    CHECK(tr(branchy).ok, "if/elseif/else/endif parses");
    CHECK(run(branchy, -0.5, 1) == C(-1, 0) && run(branchy, 0.5, 1) == C(1, 0) && run(branchy, 5, 1) == C(2, 0),
          "VM: if/elseif/else picks the right branch");
    CHECK(run("B { z = 0: if (real(pixel) > 0), z = z + 1, endif, 1 }", 1, 3) == C(3, 0), "compact if with commas");
    CHECK(errorContains("B { z = 0: if (z < 1) z = 1, |z| < 4 }", "'if' without 'endif'"), "missing endif");
    CHECK(errorContains("B { z = 0: endif, |z| < 4 }", "without 'if'"), "endif without if");
    CHECK(errorContains("B { z = 0: z = z + 1, if (z < 1) z = 1 endif }", "must end with the bailout test"), "loop ending in if");
    // new built-ins
    CHECK(tr("P { z = p4 + p5: z = z, 1 }").usesP[3] && tr("P { z = p4 + p5: z = z, 1 }").usesP[4], "p4, p5 detected");
    CHECK(run("P { z = maxit: z = z, 1 }", 0, 1) == C(100, 0), "maxit");
    CHECK(tr("W { z = whitesq: z = z, 1 }").usesWhitesq, "whitesq detected");
    // functions: inverses, rounding, Fractint specials
    C a(0.3, 0.2);
    auto fnOf = [&](const char* name, C x) {
        std::string src = std::string("F { z = ") + name + "(pixel): z = z, 1 }";
        return run(src, x, 0);
    };
    CHECK(near(fnOf("asin", fnOf("sin", a)), a) && near(fnOf("acos", fnOf("cos", a)), a) && near(fnOf("atan", fnOf("tan", a)), a),
          "asin/acos/atan invert sin/cos/tan");
    CHECK(near(fnOf("asinh", fnOf("sinh", a)), a) && near(fnOf("acosh", fnOf("cosh", C(1.3, 0.2))), C(1.3, 0.2)) &&
              near(fnOf("atanh", fnOf("tanh", a)), a),
          "asinh/acosh/atanh invert sinh/cosh/tanh");
    CHECK(near(fnOf("cotanh", a), 1.0 / std::tanh(a)) && near(fnOf("cosxx", a), std::conj(std::cos(a))), "cotanh, cosxx");
    CHECK(fnOf("round", C(-2.5, 2.5)) == C(-3, 3) && fnOf("floor", C(-0.5, 1.5)) == C(-1, 1) &&
              fnOf("ceil", C(-0.5, 1.5)) == C(0, 2) && fnOf("trunc", C(-1.7, 1.7)) == C(-1, 1),
          "round/floor/ceil/trunc work per component");
    CHECK(fnOf("zero", a) == C(0, 0) && fnOf("one", a) == C(1, 0) && near(fnOf("cabs", C(3, 4)), C(5, 0)) &&
              fnOf("real", a) == C(0.3, 0) && fnOf("imag", a) == C(0.2, 0),
          "zero, one, cabs, real, imag");
    int fnCos[4] = {1, 0, 0, 0};
    CHECK(near(run("F { z = fn1(pixel): z = z, 1 }", a, 0, fnCos), std::cos(a)), "fn1 follows the UI's choice");
    CHECK(near(run("J { z = pixel, c = p1: z = z*z + c, |z| <= 4 }", a, 2, nullptr, C(-0.8, 0.156)),
               (a * a + C(-0.8, 0.156)) * (a * a + C(-0.8, 0.156)) + C(-0.8, 0.156)),
          "p1 reaches the formula");

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
