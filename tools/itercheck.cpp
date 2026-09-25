// itercheck — checks a 2D render's iteration counts against exact arithmetic.
//
//   itercheck DUMP CENTER_RE CENTER_IM HEIGHT MAXITER [--max-mismatch F] [--bits N] [--save-truth FILE]
//   itercheck DUMP --frm FILE NAME [--max-mismatch F]
//
// DUMP comes from  fract3d --par X --render out.png --dump-iterations DUMP  for a
// Mandelbrot view rendered with Fractint bands and bailout 2 (classic.banded = 1,
// classic.bailout = 2, classic.supersample = 1). Every pixel is iterated here with
// MPFR at enough precision to be exact for this purpose; the test passes when the
// fraction of pixels whose escape iteration (or inside/outside verdict) differs is
// at most F (default 0.01). Chaotic pixels near the boundary can legitimately
// differ by a step, which is why the check allows a small fraction. --save-truth
// writes the exact result in the dump format (to measure how well-conditioned a view is:
// compare it against the truth for a center moved by a millionth of a pixel).
//
// With --frm, the render is  fract3d --formula NAME ...  of a formula from FILE (at
// its own @view, @p and @fn defaults, 256 iterations, bands on), and every pixel is
// recomputed by the CPU formula interpreter (FormulaVM, doubles): this checks that
// the GLSL the transpiler writes means the same as the interpreter. The GPU runs
// formulas in floats, so chaotic pixels may differ; the allowance covers that.
#include "formula.h"
#include "gl_util.h"

#include <mpfr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static int formulaMode(int argc, char** argv) {
    if (argc < 5) return 2;
    double maxMismatch = 0.01;
    for (int i = 5; i < argc; i++)
        if (!strcmp(argv[i], "--max-mismatch") && i + 1 < argc) maxMismatch = atof(argv[++i]);
    bool ok = false;
    std::string text = readTextFile(argv[3], &ok);
    const FormulaDef* def = nullptr;
    auto defs = parseFormulaFile(text);
    for (auto& d : defs)
        if (d.name == argv[4]) def = &d;
    if (!ok || !def) {
        fprintf(stderr, "itercheck: no formula %s in %s\n", argv[4], argv[3]);
        return 2;
    }
    int fn[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++)
        if (def->fn[i] >= 0) fn[i] = def->fn[i];
    TranspiledFormula t = transpileFormula(def->source, fn);
    if (!t.ok) {
        fprintf(stderr, "itercheck: %s\n", t.error.c_str());
        return 2;
    }
    FILE* f = fopen(argv[1], "rb");
    int32_t hdr[2];
    if (!f || fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] <= 0 || hdr[1] <= 0) return 2;
    int W = hdr[0], H = hdr[1];
    std::vector<float> got((size_t)W * H);
    if (fread(got.data(), sizeof(float), got.size(), f) != got.size()) return 2;
    fclose(f);
    // the app's view for --formula: the formula's @view, else the default 2D view
    double cx = def->hasView ? def->view[0] : -0.6, cy = def->hasView ? def->view[1] : 0.0, h = def->hasView ? def->view[2] : 3.0;
    const int maxIter = 256;
    FormulaVM::C ps[kFormulaParams];
    for (int i = 0; i < kFormulaParams; i++) ps[i] = {def->hasP[i] ? def->p[i][0] : 0.0f, def->hasP[i] ? def->p[i][1] : 0.0f};
    long wrong = 0;
    for (int row = 0; row < H; row++)
        for (int x = 0; x < W; x++) {
            int gy = H - 1 - row;
            // the shader's pixel: computed in doubles, then rounded to floats (formulas run in floats)
            FormulaVM::C pixel((float)(cx + (x + 0.5 - 0.5 * W) * (h / H)), (float)(cy + (gy + 0.5 - 0.5 * H) * (h / H)));
            FormulaVM vm(t);
            vm.setParams(ps, maxIter);
            vm.init(pixel, ((x + gy) & 1) != 0);
            int res = -1;
            for (int i = 0; i < maxIter; i++)
                if (!vm.step()) {
                    res = i;
                    break;
                }
            float g = got[(size_t)row * W + x];
            int gi = g < 0 ? -1 : (int)std::floor(g);
            if (gi != res) wrong++;
        }
    double frac = (double)wrong / got.size();
    bool pass = frac <= maxMismatch;
    printf("itercheck: %s (%dx%d): %ld pixels differ from the CPU interpreter (%.3f%%) -> %s\n", def->name.c_str(), W, H, wrong,
           frac * 100.0, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[2], "--frm")) return formulaMode(argc, argv);
    if (argc < 6) {
        fprintf(stderr, "usage: itercheck DUMP CENTER_RE CENTER_IM HEIGHT MAXITER [--max-mismatch F] [--bits N]\n");
        return 2;
    }
    const char* cre = argv[2];
    const char* cim = argv[3];
    const char* heightStr = argv[4];
    int maxIter = atoi(argv[5]);
    double maxMismatch = 0.01;
    int bits = 0;
    const char* saveTruth = nullptr;
    for (int i = 6; i < argc; i++) {
        if (!strcmp(argv[i], "--max-mismatch") && i + 1 < argc) maxMismatch = atof(argv[++i]);
        else if (!strcmp(argv[i], "--bits") && i + 1 < argc) bits = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--save-truth") && i + 1 < argc) saveTruth = argv[++i];
        else {
            fprintf(stderr, "itercheck: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    FILE* f = fopen(argv[1], "rb");
    int32_t hdr[2];
    if (!f || fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] <= 0 || hdr[1] <= 0) {
        fprintf(stderr, "itercheck: cannot read %s\n", argv[1]);
        return 2;
    }
    int W = hdr[0], H = hdr[1];
    std::vector<float> got((size_t)W * H);
    if (fread(got.data(), sizeof(float), got.size(), f) != got.size()) {
        fprintf(stderr, "itercheck: %s is truncated\n", argv[1]);
        return 2;
    }
    fclose(f);
    double height = atof(heightStr);
    if (bits == 0) bits = std::clamp((int)std::ceil(-std::log2(height / H)) + 64, 64, 8192);

    std::vector<int> truth((size_t)W * H);
    auto work = [&](int y0, int y1) {
        mpfr_t cr, ci, zr, zi, t1, t2, t3, ps, cx, cy;
        mpfr_inits2(bits, cr, ci, zr, zi, t1, t2, t3, ps, cx, cy, (mpfr_ptr)0);
        mpfr_set_str(cx, cre, 10, MPFR_RNDN);
        mpfr_set_str(cy, cim, 10, MPFR_RNDN);
        mpfr_set_str(ps, heightStr, 10, MPFR_RNDN);
        mpfr_div_si(ps, ps, H, MPFR_RNDN);
        for (int row = y0; row < y1; row++) {
            int gy = H - 1 - row;  // the dump is top-down; GL rows (and the shader) count up
            for (int x = 0; x < W; x++) {
                // the shader's pixel center: center + (pixel + 0.5 - size / 2) * pixelSize
                mpfr_mul_d(cr, ps, x + 0.5 - 0.5 * W, MPFR_RNDN);
                mpfr_add(cr, cr, cx, MPFR_RNDN);
                mpfr_mul_d(ci, ps, gy + 0.5 - 0.5 * H, MPFR_RNDN);
                mpfr_add(ci, ci, cy, MPFR_RNDN);
                mpfr_set_zero(zr, 1);
                mpfr_set_zero(zi, 1);
                int res = -1;
                for (int i = 0; i < maxIter; i++) {
                    mpfr_sqr(t1, zr, MPFR_RNDN);
                    mpfr_sqr(t2, zi, MPFR_RNDN);
                    mpfr_mul(t3, zr, zi, MPFR_RNDN);
                    mpfr_sub(zr, t1, t2, MPFR_RNDN);
                    mpfr_add(zr, zr, cr, MPFR_RNDN);
                    mpfr_mul_2ui(zi, t3, 1, MPFR_RNDN);
                    mpfr_add(zi, zi, ci, MPFR_RNDN);
                    double a = mpfr_get_d(zr, MPFR_RNDN), b = mpfr_get_d(zi, MPFR_RNDN);
                    if (a * a + b * b > 4.0) {
                        res = i;
                        break;
                    }
                }
                truth[(size_t)row * W + x] = res;
            }
        }
        mpfr_clears(cr, ci, zr, zi, t1, t2, t3, ps, cx, cy, (mpfr_ptr)0);
    };
    int n = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> ts;
    for (int k = 0; k < n; k++) ts.emplace_back(work, H * k / n, H * (k + 1) / n);
    for (auto& t : ts) t.join();

    if (saveTruth) {
        if (FILE* o = fopen(saveTruth, "wb")) {
            int32_t h2[2] = {W, H};
            fwrite(h2, sizeof h2, 1, o);
            for (int t : truth) {
                float v = t < 0 ? -1.0f : t + 0.5f;
                fwrite(&v, sizeof v, 1, o);
            }
            fclose(o);
        }
    }
    long wrong = 0, off1 = 0, verdict = 0;
    for (size_t i = 0; i < got.size(); i++) {
        int g = got[i] < 0 ? -1 : (int)std::floor(got[i]);  // banded values are iteration + 0.5
        int t = truth[i];
        if (g == t) continue;
        wrong++;
        if ((g < 0) != (t < 0)) verdict++;
        else if (std::abs(g - t) == 1) off1++;
    }
    double frac = (double)wrong / got.size();
    bool ok = frac <= maxMismatch;
    printf("itercheck: %dx%d, %d bits: %ld of %zu pixels differ (%.3f%%; %ld by one step, %ld inside/outside) -> %s\n", W, H,
           bits, wrong, got.size(), frac * 100.0, off1, verdict, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
