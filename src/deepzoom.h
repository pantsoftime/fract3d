#pragma once
// Deep zoom by perturbation theory.
//
// Past about 10^13x even doubles run out of digits. Instead of iterating every
// pixel in high precision, compute ONE reference orbit Z_n at the view center in
// arbitrary precision (MPFR, on a worker thread), then let each pixel iterate
// only its tiny difference delta_n = z_n - Z_n in plain doubles on the GPU:
//
//   delta_{n+1} = 2 Z_n delta_n + delta_n^2 + delta_c
//
// (The deepest zoom, kMinHeight = 1e-290, keeps every delta inside the range of a
// double, so no separate exponent is needed.) When |z| gets smaller than |delta|, or
// the reference runs out, the pixel restarts from the start of the reference
// ("rebasing", Zhuoran 2021), which avoids the glitches of older methods.
//
// Skipping ahead (bivariate linear approximation, "BLA", Zhuoran 2021 and
// Heiland-Allen 2022): while delta is tiny next to Z, the delta^2 term doesn't
// matter and l iterations collapse into one linear map,
//
//   delta_{m+l} = A delta_m + B delta_c,   valid while |delta_m| < R
//
// The worker builds a table of these for l = 2, 4, 8, ... at every aligned start
// m, merging pairs level by level; the GPU then jumps over long stretches of the
// orbit in one step.
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---- high-precision decimal helpers (the view center beyond double precision)
namespace hp {
int bitsForPixel(double pixelSize);  // enough precision to resolve pixels at this zoom
std::string fromDouble(double v);
bool valid(const std::string& s);
double toDouble(const std::string& s);
std::string add(const std::string& a, double delta, int bits);  // a + delta
double diffOver(const std::string& a, const std::string& b, double unit, int bits);  // (a - b) / unit
std::string lerp(const std::string& a, const std::string& b, double w, int bits);    // a + (b - a) w
std::string round(const std::string& a, int digits);  // for display: `digits` significant digits
int digitsForPixel(double pixelSize);                  // digits that locate a point to a fraction of a pixel
}  // namespace hp

// BLA error tolerance: the dropped delta^2 term may be this fraction of the kept
// linear term. Measured with tools/itercheck on a 10^20x view (whose own conditioning
// lets 0.42% of pixels change for a millionth-of-a-pixel shift): 1e-16 -> 0.36% of
// pixels differ from exact arithmetic, 1e-14 -> 0.68%, 1e-12 -> 1.8%. Speed-up on a
// 300000-iteration minibrot at 10^31x: 5x, 6x, 8x. Accuracy wins.
inline constexpr double kBlaEpsilon = 1e-16;

struct RefOrbitRequest {
    std::string re, im;  // reference point (Mandelbrot: c; Julia: z0)
    bool julia = false;
    double jre = 0, jim = 0;  // Julia constant
    int maxIter = 0;
    int bits = 64;
    float bailout = 64;
    double dcMax = 0;       // largest |delta_c| the reference will serve (BLA validity); 0 for Julia sets
    double blaEps = 0;      // BLA error tolerance (relative); 0: no table
    bool operator==(const RefOrbitRequest& o) const {
        return re == o.re && im == o.im && julia == o.julia && jre == o.jre && jim == o.jim && maxIter == o.maxIter &&
               bits == o.bits && bailout == o.bailout && dcMax == o.dcMax && blaEps == o.blaEps;
    }
};

// The BLA table: for each level j >= 1 (skip length 2^j), entries for the starts
// m = 1 + k 2^j, each (A.re, A.im, B.re, B.im, R^2, 0) - 6 doubles, the layout of
// the shader's BlaStep. levelOffset[j] is the index of level j's first entry.
struct BlaTable {
    std::vector<double> data;
    std::vector<int> levelOffset, levelCount;  // [0] unused
    int levels() const { return (int)levelOffset.size() - 1; }
};

// Computes reference orbits on a background thread.
class RefOrbitWorker {
public:
    ~RefOrbitWorker();
    void request(const RefOrbitRequest& r);  // restarts if different from the current one
    bool ready() const { return ready_; }
    float progress() const { return progress_; }
    const RefOrbitRequest& current() const { return req_; }
    // Takes the finished orbit (x, y doubles, Z_0 first). Only valid when ready().
    const std::vector<double>& orbit() const { return *orbit_; }
    std::shared_ptr<const std::vector<double>> orbitPtr() const { return orbit_; }
    int version() const { return version_; }  // bumped whenever a new orbit is ready
    const BlaTable& bla() const { return *bla_; }  // only valid when ready()
    std::shared_ptr<const BlaTable> blaPtr() const { return bla_; }

private:
    void stop();
    RefOrbitRequest req_;
    std::thread thread_;
    std::atomic<bool> cancel_{false}, ready_{false};
    std::atomic<float> progress_{0};
    std::shared_ptr<const std::vector<double>> orbit_ = std::make_shared<std::vector<double>>();
    std::shared_ptr<const BlaTable> bla_ = std::make_shared<BlaTable>();
    std::atomic<int> version_{0};
    bool running_ = false;
};

// ---- series approximation: skipping the stretch every pixel shares
//
// Deep views have a long start (often 95% of each pixel's iterations) during which
// every pixel's orbit moves almost exactly with the view center's. There, a pixel's
// offset from the center's orbit is a power series in the pixel's position,
//
//   eta_n(u) = c_1 u + c_2 u^2 + ... + c_K u^K,   u = (pixel - center) / R,
//
// R the distance from the center to the image's corners. The worker advances the
// coefficients one iteration at a time (eta' = 2 z eta + eta^2 + R u for the
// Mandelbrot set) and checks them against probe points iterated exactly (the corners,
// edge middles and half-way points: where the series is least accurate); the skip
// ends at the last iteration where every probe agrees to kSeriesTolerance and no
// pixel can have escaped yet. Every pixel then starts there by evaluating the
// polynomial. (Kalles Fraktaler made this popular for deep zooms.)
inline constexpr int kSeriesTerms = 24;
inline constexpr double kSeriesTolerance = 1e-12;
inline constexpr double kSeriesMinSaving = 0.75;  // used only if the kernel's work drops below this share

struct SeriesRequest {
    std::shared_ptr<const std::vector<double>> orbit;  // the reference orbit Z_n
    std::shared_ptr<const BlaTable> bla;               // its skip-ahead table (may be empty)
    int orbitVersion = -1;
    bool julia = false;
    double dc0[2] = {0, 0};   // view center - reference point (plane units)
    double half[2] = {0, 0};  // half the image's width and height (plane units)
    int maxIter = 0;
    double bailout = 2;
    bool sameView(const SeriesRequest& o) const {
        return orbitVersion == o.orbitVersion && julia == o.julia && dc0[0] == o.dc0[0] && dc0[1] == o.dc0[1] &&
               half[0] == o.half[0] && half[1] == o.half[1] && maxIter == o.maxIter && bailout == o.bailout;
    }
};
struct SeriesResult {
    int skip = 0;                // iterations every pixel skips (0: no series)
    double half[2] = {0, 0};     // the image the series was made for (half width, half height, plane units)
    double invR = 0;             // 1 / R
    double base[2] = {0, 0};     // the view center's delta after the skip
    std::vector<double> coef;    // c_1 .. c_K (re, im)
};

class SeriesWorker {
public:
    ~SeriesWorker();
    void request(const SeriesRequest& r);  // restarts unless it's the same view
    // The result for exactly this view, or nullptr (not ready, or a different view).
    const SeriesResult* resultFor(const SeriesRequest& r) const;
    void wait() const;

private:
    void stop();
    SeriesRequest req_;
    SeriesResult result_;
    std::thread thread_;
    std::atomic<bool> cancel_{false}, ready_{false};
    bool running_ = false;
};
