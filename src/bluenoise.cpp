#include "bluenoise.h"

#include <cmath>

namespace {
// Keeps the Gaussian "energy" of the set pixels up to date incrementally:
// toggling one pixel adds or removes its (toroidal) kernel everywhere.
struct Field {
    int n;
    std::vector<float> kernel, energy;
    std::vector<uint8_t> on;
    explicit Field(int size) : n(size), kernel(size * size), energy(size * size, 0.0f), on(size * size, 0) {
        const float sigma = 1.5f;
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                int dx = std::min(x, n - x), dy = std::min(y, n - y);
                kernel[y * n + x] = std::exp(-(dx * dx + dy * dy) / (2.0f * sigma * sigma));
            }
    }
    void toggle(int p, bool set) {
        on[p] = set;
        float s = set ? 1.0f : -1.0f;
        int px = p % n, py = p / n;
        for (int y = 0; y < n; y++) {
            int ky = ((y - py) % n + n) % n;
            for (int x = 0; x < n; x++) energy[y * n + x] += s * kernel[ky * n + ((x - px) % n + n) % n];
        }
    }
    int tightestCluster() const {  // set pixel with the most set neighbours
        int best = -1;
        for (int i = 0; i < n * n; i++)
            if (on[i] && (best < 0 || energy[i] > energy[best])) best = i;
        return best;
    }
    int largestVoid() const {  // empty pixel farthest from set ones
        int best = -1;
        for (int i = 0; i < n * n; i++)
            if (!on[i] && (best < 0 || energy[i] < energy[best])) best = i;
        return best;
    }
};

uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
}  // namespace

std::vector<float> makeBlueNoise(int size, uint32_t seed) {
    const int N = size * size;
    Field f(size);
    // 1. random initial pattern (~10% set), then relax it: move the tightest
    //    cluster into the largest void until that stops changing anything
    uint32_t rng = seed | 1u;
    int ones = N / 10;
    for (int placed = 0; placed < ones;) {
        int p = xorshift(rng) % N;
        if (!f.on[p]) f.toggle(p, true), placed++;
    }
    for (int guard = 0; guard < N * 4; guard++) {
        int c = f.tightestCluster();
        f.toggle(c, false);
        int v = f.largestVoid();
        if (v == c) {
            f.toggle(c, true);
            break;
        }
        f.toggle(v, true);
    }
    std::vector<int> rank(N, -1);
    Field g = f;
    // 2. rank the initial points by removing tightest clusters
    for (int r = ones - 1; r >= 0; r--) {
        int c = g.tightestCluster();
        g.toggle(c, false);
        rank[c] = r;
    }
    // 3. rank the rest by filling the largest voids, starting from the initial pattern
    for (int r = ones; r < N; r++) {
        int v = f.largestVoid();
        f.toggle(v, true);
        rank[v] = r;
    }
    std::vector<float> out(N);
    for (int i = 0; i < N; i++) out[i] = (rank[i] + 0.5f) / N;
    return out;
}
