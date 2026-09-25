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
#include <atomic>
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

struct RefOrbitRequest {
    std::string re, im;  // reference point (Mandelbrot: c; Julia: z0)
    bool julia = false;
    double jre = 0, jim = 0;  // Julia constant
    int maxIter = 0;
    int bits = 64;
    float bailout = 64;
    bool operator==(const RefOrbitRequest& o) const {
        return re == o.re && im == o.im && julia == o.julia && jre == o.jre && jim == o.jim && maxIter == o.maxIter &&
               bits == o.bits && bailout == o.bailout;
    }
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
    const std::vector<double>& orbit() const { return orbit_; }
    int version() const { return version_; }  // bumped whenever a new orbit is ready

private:
    void stop();
    RefOrbitRequest req_;
    std::thread thread_;
    std::atomic<bool> cancel_{false}, ready_{false};
    std::atomic<float> progress_{0};
    std::vector<double> orbit_;
    std::atomic<int> version_{0};
    bool running_ = false;
};
