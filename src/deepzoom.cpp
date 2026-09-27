#include "deepzoom.h"

#include <mpfr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <complex>
#include <limits>

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

std::string round(const std::string& a, int digits) {
    F x(std::clamp((int)(digits * 3.33) + 16, 64, 8192));
    if (!set(x, a)) return a;
    char* out = nullptr;
    mpfr_asprintf(&out, "%.*Rg", std::clamp(digits, 1, 2000), x.v);
    std::string s = out ? out : a;
    mpfr_free_str(out);
    return s;
}

int digitsForPixel(double pixelSize) { return std::clamp((int)std::ceil(-std::log10(std::max(pixelSize, 1e-300))) + 2, 6, 400); }

}  // namespace hp

// ------------------------------------------------------------------ BLA
// Heiland-Allen, "Deep zoom theory and practice (again)", 2022. One iteration at
// reference index m is  delta' = A delta + B dc  with A = 2 Z_m, B = 1, dropping
// delta^2, which is small next to 2 Z delta while |delta| < eps |A|. Two
// consecutive steps x then y merge into  A = Ay Ax, B = Ay Bx + By, valid while
// delta stays inside x's radius and x's result inside y's:
//   R = min(Rx, (Ry - |Bx| dcMax) / |Ax|).
static BlaTable buildBla(const std::vector<double>& z, double eps, double dcMax, const std::atomic<bool>& cancel) {
    BlaTable t;
    int n = (int)(z.size() / 2);
    int steps = n - 2;  // single steps m = 1 .. n-2 (the shader never skips onto the last entry)
    if (steps < 2) return t;
    struct E { double ar, ai, br, bi, r; };
    std::vector<E> cur(steps);
    for (int k = 0; k < steps; k++) {
        double zr = z[2 * (k + 1)], zi = z[2 * (k + 1) + 1];
        double ar = 2 * zr, ai = 2 * zi;
        cur[k] = {ar, ai, 1, 0, eps * std::hypot(ar, ai)};
    }
    t.levelOffset.push_back(0);
    t.levelCount.push_back(0);
    while (cur.size() >= 2 && !cancel) {
        std::vector<E> next(cur.size() / 2);
        for (size_t k = 0; k < next.size(); k++) {
            const E &x = cur[2 * k], &y = cur[2 * k + 1];
            E e;
            e.ar = y.ar * x.ar - y.ai * x.ai;
            e.ai = y.ar * x.ai + y.ai * x.ar;
            e.br = y.ar * x.br - y.ai * x.bi + y.br;
            e.bi = y.ar * x.bi + y.ai * x.br + y.bi;
            double ax = std::hypot(x.ar, x.ai), bx = std::hypot(x.br, x.bi);
            e.r = std::min(x.r, std::max(0.0, (y.r - bx * dcMax) / std::max(ax, 1e-300)));
            if (!std::isfinite(e.ar) || !std::isfinite(e.ai) || !std::isfinite(e.br) || !std::isfinite(e.bi)) e = {0, 0, 0, 0, 0};
            next[k] = e;
        }
        t.levelOffset.push_back((int)(t.data.size() / 6));
        t.levelCount.push_back((int)next.size());
        for (auto& e : next) t.data.insert(t.data.end(), {e.ar, e.ai, e.br, e.bi, e.r * e.r, 0.0});
        cur.swap(next);
    }
    return t;
}

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
        std::vector<double> out;
        out.reserve(2 * (size_t)std::min(r.maxIter + 1, 1 << 22));
        double bail2 = (double)r.bailout * r.bailout;
        for (int n = 0; n <= r.maxIter && !cancel_; n++) {
            double x = mpfr_get_d(zr, MPFR_RNDN), y = mpfr_get_d(zi, MPFR_RNDN);
            out.push_back(x);
            out.push_back(y);
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
        // the skip-ahead table (large orbits would need too much memory: they iterate normally)
        BlaTable bla;
        if (r.blaEps > 0 && out.size() / 2 <= (1u << 21) && !cancel_) bla = buildBla(out, r.blaEps, r.dcMax, cancel_);
        if (!cancel_) {
            bla_ = std::make_shared<const BlaTable>(std::move(bla));
            orbit_ = std::make_shared<const std::vector<double>>(std::move(out));
            progress_ = 1;
            version_++;
            ready_ = true;
        }
    });
}

// ------------------------------------------------------------------ series approximation
// The perturbation kernel's loop in doubles: how many trips (single steps and skip-ahead
// jumps) a pixel at delta_c = dc takes from (m, i, eps) until iteration `until`, escape or
// the limit. Used to judge whether the series saves work that skip-ahead didn't already.
long advanceOrbit(const std::vector<double>& z, const BlaTable* t, double dcMax, OrbitProbe& p, int until, int maxIter,
                  long maxTrips, double bail) {
    using C = std::complex<double>;
    if (p.done) return 0;
    int refLen = (int)(z.size() / 2), levels = t ? std::min(t->levels(), 30) : 0;
    C cD(p.dc[0], p.dc[1]), eps(p.eps[0], p.eps[1]);
    bool jumps = levels > 0 && std::norm(cD) <= dcMax * dcMax;
    double bail2 = bail * bail;
    auto Z = [&](int k) { return C(z[2 * k], z[2 * k + 1]); };
    long trips = 0;
    int m = p.m, i = p.i;
    for (; i < until && trips < maxTrips; i++) {
        trips++;
        if (jumps && m >= 1) {
            int mm = m - 1;
            double d2 = std::norm(eps);
            int jmax = std::min(mm == 0 ? levels : __builtin_ctz(mm), levels), best = 0;
            const double* bs = nullptr;
            for (int j = 1; j <= jmax; j++) {
                int l = 1 << j, k = mm >> j;
                if (k >= t->levelCount[j] || m + l >= refLen - 1 || i + l >= maxIter) break;
                const double* s = &t->data[6 * (size_t)(t->levelOffset[j] + k)];
                if (d2 >= s[4]) break;
                best = j;
                bs = s;
            }
            if (best > 0) {
                int l = 1 << best;
                eps = C(bs[0], bs[1]) * eps + C(bs[2], bs[3]) * cD;
                m += l;
                i += l - 1;
                continue;
            }
        }
        eps = (2.0 * Z(m) + eps) * eps + cD;
        m++;
        C zf = Z(m) + eps;
        if (std::norm(zf) > bail2) {
            p.done = true;
            break;
        }
        if (m >= refLen - 1 || std::norm(zf) < std::norm(eps)) {
            eps = zf - Z(0);
            m = 0;
        }
    }
    p.eps[0] = eps.real(), p.eps[1] = eps.imag();
    p.m = m, p.i = i;
    if (i >= maxIter) p.done = true;
    return trips;
}

// The trips a pixel at delta_c = dc takes from (m, i, eps) until iteration `until`.
static long orbitTrips(const std::vector<double>& z, const BlaTable& t, bool julia, std::complex<double> dc,
                       std::complex<double> eps, int m, int i, int until, int maxIter, double bail) {
    OrbitProbe p;
    if (!julia) p.dc[0] = dc.real(), p.dc[1] = dc.imag();
    p.eps[0] = eps.real(), p.eps[1] = eps.imag();
    p.m = m, p.i = i;
    return advanceOrbit(z, &t, 1e300, p, until, maxIter, std::numeric_limits<long>::max(), bail);
}

SeriesWorker::~SeriesWorker() { stop(); }

void SeriesWorker::stop() {
    if (running_) {
        cancel_ = true;
        thread_.join();
        running_ = false;
    }
}

const SeriesResult* SeriesWorker::resultFor(const SeriesRequest& r) const {
    return ready_ && req_.sameView(r) ? &result_ : nullptr;
}

void SeriesWorker::wait() const {
    while (running_ && !ready_) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void SeriesWorker::request(const SeriesRequest& r) {
    if (running_ && req_.sameView(r)) return;
    stop();
    req_ = r;
    ready_ = false;
    cancel_ = false;
    running_ = true;
    thread_ = std::thread([this, r] {
        using C = std::complex<double>;
        const std::vector<double>& z = *r.orbit;
        int L = (int)(z.size() / 2);
        const int K = kSeriesTerms;
        double R = std::hypot(r.half[0], r.half[1]);
        SeriesResult out;
        if (R <= 0 || L < 3) {
            result_ = out;
            ready_ = true;
            return;
        }
        out.invR = 1.0 / R;
        out.half[0] = r.half[0], out.half[1] = r.half[1];
        const bool mandel = !r.julia;
        const C dc0(r.dc0[0], r.dc0[1]);
        // the view center's own delta, and the series around it (c[p-1] is c_p)
        C W = r.julia ? dc0 : C(0);
        std::vector<C> c(K, C(0)), nc(K);
        if (r.julia) c[0] = R;  // eta_0 = v = R u
        // probes: corners, edge middles, half-way to the corners; their eta iterated exactly
        std::vector<C> pv, peta;
        for (double fx : {-1.0, 0.0, 1.0})
            for (double fy : {-1.0, 0.0, 1.0})
                if (fx != 0 || fy != 0) pv.push_back(C(fx * r.half[0], fy * r.half[1]));
        for (double fx : {-0.5, 0.5})
            for (double fy : {-0.5, 0.5}) pv.push_back(C(fx * r.half[0], fy * r.half[1]));
        for (auto& v : pv) peta.push_back(r.julia ? v : C(0));
        std::vector<C> saved = c;
        C savedW = W;
        int best = 0;
        double bail = std::max(r.bailout, 2.0);
        int limit = std::min(L - 2, r.maxIter - 1);
        for (int n = 0; n < limit && !cancel_; n++) {
            C Z(z[2 * n], z[2 * n + 1]), zc = Z + W;
            // eta' = 2 zc eta + eta^2 (+ v): coefficient by coefficient
            for (int p = 0; p < K; p++) {
                C s = 2.0 * zc * c[p];
                for (int i = 0; i < p; i++) s += c[i] * c[p - 1 - i];  // powers (i+1) + (p-i) = p+1
                nc[p] = s;
            }
            if (mandel) nc[0] += R;
            c.swap(nc);
            for (size_t k = 0; k < pv.size(); k++) peta[k] = 2.0 * zc * peta[k] + peta[k] * peta[k] + (mandel ? pv[k] : C(0));
            W = 2.0 * Z * W + W * W + (mandel ? dc0 : C(0));
            // valid after n+1 iterations? every probe must agree, and no pixel may have escaped
            bool ok = true;
            double bound = 0;
            for (auto& cp : c) {
                bound += std::abs(cp);
                if (!std::isfinite(cp.real()) || !std::isfinite(cp.imag())) ok = false;
            }
            C Zn(z[2 * (n + 1)], z[2 * (n + 1) + 1]);
            if (std::abs(Zn + W) + bound >= bail) ok = false;
            for (size_t k = 0; k < pv.size() && ok; k++) {
                C u = pv[k] / R, e(0), up = u;
                for (int p = 0; p < K; p++, up *= u) e += c[p] * up;
                double err = std::abs(e - peta[k]);
                if (!(err <= kSeriesTolerance * std::abs(peta[k]) + 1e-300)) ok = false;
            }
            if (!ok) break;
            best = n + 1;
            saved = c;
            savedW = W;
        }
        if (cancel_) return;
        // Worth it? Skip-ahead may already cover the shared start cheaply (near a
        // minibrot, jumps span most of a period from the first iteration). Count the
        // kernel's loop trips for the probes both ways and keep the series only when
        // it removes a good share of them; the polynomial costs about K trips.
        if (best > 0 && r.bla && r.bla->levels() > 0) {
            double without = 0, with = 0;
            for (auto& v : pv) {
                C dc = dc0 + v, u = v / R, e(0), up = u;
                for (int p = 0; p < K; p++, up *= u) e += saved[p] * up;
                C eps0 = r.julia ? dc : C(0);
                without += (double)orbitTrips(z, *r.bla, r.julia, dc, eps0, 0, 0, r.maxIter, r.maxIter, bail);
                with += K + (double)orbitTrips(z, *r.bla, r.julia, dc, savedW + e, best, best, r.maxIter, r.maxIter, bail);
            }
            if (!(with < kSeriesMinSaving * without)) best = 0;
        }
        out.skip = best;
        out.base[0] = savedW.real();
        out.base[1] = savedW.imag();
        for (auto& cp : saved) out.coef.insert(out.coef.end(), {cp.real(), cp.imag()});
        result_ = out;
        ready_ = true;
    });
}
