// imgdiff — compares two images for the render regression tests.
//
//   imgdiff expected.png actual.png [--exact] [--max-mean X] [--max-bad-fraction F] [--bad-threshold T]
//
// Passes (exit 0) when the images have the same size and
//   --exact:  every channel of every pixel matches, or otherwise
//   mean |difference| <= max-mean (0-255 scale, default 1.5) and the fraction
//   of pixels with any channel off by more than bad-threshold (default 48)
//   is <= max-bad-fraction (default 0.005).
// The tolerance absorbs driver/GPU differences in floating-point rounding.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: imgdiff expected.png actual.png [--exact] [--max-mean X] [--max-bad-fraction F] [--bad-threshold T]\n");
        return 2;
    }
    bool exact = false;
    double maxMean = 1.5, maxBad = 0.005;
    int badThreshold = 48;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--exact")) exact = true;
        else if (!strcmp(argv[i], "--max-mean") && i + 1 < argc) maxMean = atof(argv[++i]);
        else if (!strcmp(argv[i], "--max-bad-fraction") && i + 1 < argc) maxBad = atof(argv[++i]);
        else if (!strcmp(argv[i], "--bad-threshold") && i + 1 < argc) badThreshold = atoi(argv[++i]);
        else {
            fprintf(stderr, "imgdiff: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    int w1, h1, w2, h2, n;
    std::unique_ptr<unsigned char, void (*)(void*)> a(stbi_load(argv[1], &w1, &h1, &n, 3), stbi_image_free);
    std::unique_ptr<unsigned char, void (*)(void*)> b(stbi_load(argv[2], &w2, &h2, &n, 3), stbi_image_free);
    if (!a || !b) {
        fprintf(stderr, "imgdiff: cannot read %s\n", !a ? argv[1] : argv[2]);
        return 2;
    }
    if (w1 != w2 || h1 != h2) {
        fprintf(stderr, "imgdiff: size differs: %dx%d vs %dx%d\n", w1, h1, w2, h2);
        return 1;
    }
    long px = (long)w1 * h1, bad = 0;
    double sum = 0;
    int maxDiff = 0;
    for (long i = 0; i < px; i++) {
        int worst = 0;
        for (int c = 0; c < 3; c++) {
            int d = std::abs((int)a.get()[i * 3 + c] - (int)b.get()[i * 3 + c]);
            sum += d;
            worst = d > worst ? d : worst;
        }
        maxDiff = worst > maxDiff ? worst : maxDiff;
        if (worst > badThreshold) bad++;
    }
    double mean = sum / (px * 3.0), badFrac = (double)bad / px;
    bool ok = exact ? maxDiff == 0 : (mean <= maxMean && badFrac <= maxBad);
    printf("imgdiff: mean %.3f  max %d  bad pixels %.4f%%  -> %s\n", mean, maxDiff, badFrac * 100.0, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
