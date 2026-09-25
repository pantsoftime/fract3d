#include "deepzoom.h"

#include <mpfr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hp {

int bitsForPixel(double pixelSize) {
    double b = -std::log2(std::max(pixelSize, 1e-300)) + 48;  // digits to resolve a pixel, plus headroom
    return std::clamp((int)std::ceil(b), 64, 4096);
}

// RAII MPFR number
struct F {
    mpfr_t v;
    explicit F(int bits) { mpfr_init2(v, bits); }
    ~F() { mpfr_clear(v); }
    F(const F&) = delete;
    F& operator=(const F&) = delete;
};

static bool set(F& f, const std::string& s) { return mpfr_set_str(f.v, s.c_str(), 10, MPFR_RNDN) == 0; }

static std::string str(const F& f, int bits) {
    // enough decimal digits to round-trip `bits`
    int digits = (int)std::ceil(bits * 0.30103) + 2;
    char fmt[32];
    snprintf(fmt, sizeof fmt, "%%.%dRg", digits);
    char* out = nullptr;
    mpfr_asprintf(&out, fmt, f.v);
    std::string s = out ? out : "0";
    mpfr_free_str(out);
    return s;
}

std::string fromDouble(double v) {
    char b[40];
    snprintf(b, sizeof b, "%.17g", v);
    return b;
}

bool valid(const std::string& s) {
    if (s.empty()) return false;
    F f(64);
    return set(f, s) && mpfr_number_p(f.v);
}

double toDouble(const std::string& s) {
    F f(64);
    return set(f, s) ? mpfr_get_d(f.v, MPFR_RNDN) : 0.0;
}

std::string add(const std::string& a, double delta, int bits) {
    F x(bits);
    if (!set(x, a)) return fromDouble(delta);
    mpfr_add_d(x.v, x.v, delta, MPFR_RNDN);
    return str(x, bits);
}

double diffOver(const std::string& a, const std::string& b, double unit, int bits) {
    F x(bits), y(bits);
    if (!set(x, a) || !set(y, b)) return 0;
    mpfr_sub(x.v, x.v, y.v, MPFR_RNDN);
    mpfr_div_d(x.v, x.v, unit, MPFR_RNDN);
    return mpfr_get_d(x.v, MPFR_RNDN);
}

std::string lerp(const std::string& a, const std::string& b, double w, int bits) {
    F x(bits), y(bits);
    if (!set(x, a) || !set(y, b)) return a;
    mpfr_sub(y.v, y.v, x.v, MPFR_RNDN);
    mpfr_mul_d(y.v, y.v, w, MPFR_RNDN);
    mpfr_add(x.v, x.v, y.v, MPFR_RNDN);
    return str(x, bits);
}

}  // namespace hp

// ------------------------------------------------------------------ worker
RefOrbitWorker::~RefOrbitWorker() { stop(); }

void RefOrbitWorker::stop() {
    if (running_) {
        cancel_ = true;
        thread_.join();
        running_ = false;
    }
}

void RefOrbitWorker::request(const RefOrbitRequest& r) {
    if (running_ && r == req_) return;  // already on it (or done)
    stop();
    req_ = r;
    ready_ = false;
    cancel_ = false;
    progress_ = 0;
    running_ = true;
    thread_ = std::thread([this, r] {
        int bits = r.bits;
        mpfr_t zr, zi, cr, ci, t1, t2, t3;
        mpfr_inits2(bits, zr, zi, cr, ci, t1, t2, t3, (mpfr_ptr)0);
        if (r.julia) {
            mpfr_set_str(zr, r.re.c_str(), 10, MPFR_RNDN);
            mpfr_set_str(zi, r.im.c_str(), 10, MPFR_RNDN);
            mpfr_set_d(cr, r.jre, MPFR_RNDN);
            mpfr_set_d(ci, r.jim, MPFR_RNDN);
        } else {
            mpfr_set_zero(zr, 1);
            mpfr_set_zero(zi, 1);
            mpfr_set_str(cr, r.re.c_str(), 10, MPFR_RNDN);
            mpfr_set_str(ci, r.im.c_str(), 10, MPFR_RNDN);
        }
        std::vector<float> out;
        out.reserve(2 * (size_t)std::min(r.maxIter + 1, 1 << 22));
        double bail2 = (double)r.bailout * r.bailout;
        for (int n = 0; n <= r.maxIter && !cancel_; n++) {
            double x = mpfr_get_d(zr, MPFR_RNDN), y = mpfr_get_d(zi, MPFR_RNDN);
            out.push_back((float)x);
            out.push_back((float)y);
            if (x * x + y * y > bail2) break;  // the reference escaped: pixels rebase at the end
            // z = z^2 + c
            mpfr_sqr(t1, zr, MPFR_RNDN);
            mpfr_sqr(t2, zi, MPFR_RNDN);
            mpfr_mul(t3, zr, zi, MPFR_RNDN);
            mpfr_sub(zr, t1, t2, MPFR_RNDN);
            mpfr_add(zr, zr, cr, MPFR_RNDN);
            mpfr_mul_2ui(zi, t3, 1, MPFR_RNDN);
            mpfr_add(zi, zi, ci, MPFR_RNDN);
            if ((n & 1023) == 0) progress_ = (float)n / std::max(r.maxIter, 1);
        }
        mpfr_clears(zr, zi, cr, ci, t1, t2, t3, (mpfr_ptr)0);
        if (!cancel_) {
            orbit_ = std::move(out);
            progress_ = 1;
            version_++;
            ready_ = true;
        }
    });
}
