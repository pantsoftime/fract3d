// Flies the autopilot (src/autopilot.cpp) through analytic shapes on the CPU, with sensor
// readings delayed as the GPU's are, and checks that it never touches a surface, keeps
// the clearance it was given, covers ground, and gets out of a dead end.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "autopilot.h"

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("%-64s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

static float sphere(const Vec3& p) { return p.length() - 1.0f; }
static float torusTube(const Vec3& p) {  // inside a donut-shaped tunnel: ring radius 3, tube radius 0.6
    float q = std::sqrt(p.x * p.x + p.z * p.z) - 3.0f;
    return 0.6f - std::sqrt(q * q + p.y * p.y);
}
static float deadEnd(const Vec3& p) {  // inside a straight tube along z, closed at z = 4
    return std::min(0.5f - std::sqrt(p.x * p.x + p.y * p.y), 4.0f - p.z);
}
static float city(const Vec3& p) {  // pillars 0.9 wide, 2 apart, 6 tall: streets to weave through
    Vec3 c(std::fmod(std::fabs(p.x) + 1.0f, 2.0f) - 1.0f, p.y, std::fmod(std::fabs(p.z) + 1.0f, 2.0f) - 1.0f);
    Vec3 d(std::fabs(c.x) - 0.45f, std::fabs(c.y) - 3.0f, std::fabs(c.z) - 0.45f);
    Vec3 m(std::max(d.x, 0.f), std::max(d.y, 0.f), std::max(d.z, 0.f));
    return m.length() + std::min(std::max(d.x, std::max(d.y, d.z)), 0.0f);
}

// total angle swept around the y axis (how far it went round)
static float swept(const FlightLog& L) {
    float a = 0, prev = std::atan2(L.pos[0].x, L.pos[0].z);
    for (auto& p : L.pos) {
        float b = std::atan2(p.x, p.z), d = b - prev;
        if (d > 3.14159265f) d -= 6.2831853f;
        if (d < -3.14159265f) d += 6.2831853f;
        a += d;
        prev = b;
    }
    return std::fabs(a) * 57.29578f;
}
// How smoothly the camera turned: the 95th-percentile turn rate (degrees per second) and the
// largest angular acceleration (degrees per second squared). The autopilot is a camera
// operator as much as a pilot: a view that snaps from frame to frame is unwatchable.
struct Smooth {
    float turnP95 = 0, accelMax = 0;
};
static Smooth smoothness(const FlightLog& L, float dt) {
    std::vector<Vec3> w;  // angular velocity vectors
    for (size_t i = 1; i < L.look.size(); i++) w.push_back(L.look[i - 1].cross(L.look[i]) * (1.0f / dt));
    std::vector<float> rate;
    Smooth s;
    for (size_t i = 0; i < w.size(); i++) {
        rate.push_back(w[i].length() * 57.29578f);
        if (i > 0) s.accelMax = std::max(s.accelMax, (w[i] - w[i - 1]).length() / dt * 57.29578f);
    }
    std::sort(rate.begin(), rate.end());
    s.turnP95 = rate.empty() ? 0 : rate[rate.size() * 95 / 100];
    return s;
}
static float meanDeLastHalf(const FlightLog& L) {
    double s = 0;
    size_t n = 0;
    for (size_t i = L.de.size() / 2; i < L.de.size(); i++) s += L.de[i], n++;
    return (float)(s / std::max<size_t>(n, 1));
}

int main() {
    Autopilot ap;
    for (float dt : {1 / 60.0f, 1 / 20.0f}) {  // smooth and choppy frame rates
        ap.engage(FlightStyle::Around, Vec3(0, 0, 1), 0.5f, 7);
        FlightLog a = simulateFlight(sphere, ap, Vec3(0, 0.3f, -3), 90, dt);
        float m = meanDeLastHalf(a);
        printf("  around a sphere at %2.0f fps: closest %.3f, mean height %.3f (asked 0.5), %.0f units flown\n", 1 / dt, a.minDe, m, a.travelled);
        check(a.minDe > 0.25f, "around: never comes closer than half the clearance");
        check(m > 0.35f && m < 0.8f, "around: holds about the height it was given");
        check(a.travelled > 25, "around: keeps flying");
        Smooth sm = smoothness(a, dt);
        printf("    view turns at %.0f deg/s (95%%), accelerates at most %.0f deg/s^2\n", sm.turnP95, sm.accelMax);
        check(sm.turnP95 < 70 && sm.accelMax < 400, "around: the view turns smoothly");
    }
    ap.engage(FlightStyle::Through, Vec3(1, 0, 0), 0.3f, 7);
    FlightLog t = simulateFlight(torusTube, ap, Vec3(0, 0, -3), 90, 1 / 60.0f);
    printf("  donut tunnel: closest %.3f, %.0f degrees around the ring\n", t.minDe, swept(t));
    check(t.minDe > 0.05f, "through: never touches the tunnel wall");
    check(swept(t) > 360, "through: follows a curving tunnel all the way round");
    Smooth st = smoothness(t, 1 / 60.0f);
    printf("    view turns at %.0f deg/s (95%%), accelerates at most %.0f deg/s^2\n", st.turnP95, st.accelMax);
    check(st.turnP95 < 70 && st.accelMax < 400, "through: the view turns smoothly in a tunnel");

    ap.engage(FlightStyle::Through, Vec3(0, 0, 1), 0.2f, 7);
    FlightLog d = simulateFlight(deadEnd, ap, Vec3(0, 0, 0), 40, 1 / 60.0f);
    float far = -1e9f;
    for (auto& p : d.pos) far = std::max(far, p.z);
    printf("  dead end: closest %.3f, reached z %.2f (end wall at 4), now at z %.2f\n", d.minDe, far, d.pos.back().z);
    check(d.minDe > 0.05f, "dead end: never touches a wall");
    check(far > 2.5f && d.pos.back().z < 0, "dead end: goes in, turns around and comes back out");

    ap.engage(FlightStyle::Through, Vec3(0, 0, 1), 0.25f, 7);
    FlightLog c = simulateFlight(city, ap, Vec3(1, 0, 1), 90, 1 / 60.0f);
    printf("  city of pillars: closest %.3f, %.0f units flown\n", c.minDe, c.travelled);
    check(c.minDe > 0.05f, "through: weaves between pillars without touching one");
    check(c.travelled > 20, "through: keeps exploring");
    Smooth sc = smoothness(c, 1 / 60.0f);
    printf("    view turns at %.0f deg/s (95%%), accelerates at most %.0f deg/s^2\n", sc.turnP95, sc.accelMax);
    check(sc.turnP95 < 70 && sc.accelMax < 400, "through: the view turns smoothly weaving between pillars");
    // Two sizes (Autopilot::clearanceOpen): big out in the open, small where it's enclosed
    auto sizeRange = [](const FlightLog& L, float t0, float t1, float dt) {
        float lo = 1e30f, hi = 0;
        for (size_t i = (size_t)(t0 / dt); i < L.size.size() && i < (size_t)(t1 / dt); i++) lo = std::min(lo, L.size[i]), hi = std::max(hi, L.size[i]);
        return std::make_pair(lo, hi);
    };
    ap.engage(FlightStyle::Through, Vec3(0, 0, 1), 0.02f, 7, 0.3f);
    FlightLog o = simulateFlight(sphere, ap, Vec3(0, 0.2f, -2.2f), 120, 1 / 60.0f);
    auto early = sizeRange(o, 5, 12, 1 / 60.0f), dive = sizeRange(o, 12, 70, 1 / 60.0f), late = sizeRange(o, 70, 120, 1 / 60.0f);
    printf("  outside a sphere: size %.3f-%.3f, then diving %.3f-%.3f, then %.3f-%.3f; closest %.4f\n", early.first, early.second,
           dive.first, dive.second, late.first, late.second, o.minDe);
    check(early.first > 0.2f, "two sizes: out in the open it flies big");
    check(dive.first < 0.04f, "two sizes: it dives to the surface, small, to look for a way in");
    check(late.second > 0.2f, "two sizes: finding none, it climbs back out");
    check(o.minDe > 0.0f, "two sizes: never touches, diving or not");
    ap.engage(FlightStyle::Through, Vec3(1, 0, 0), 0.05f, 7, 1.0f);
    FlightLog in = simulateFlight(torusTube, ap, Vec3(0, 0, -3), 60, 1 / 60.0f);
    auto tube = sizeRange(in, 10, 60, 1 / 60.0f);
    printf("  in the donut tunnel: size %.3f-%.3f, closest %.3f\n", tube.first, tube.second, in.minDe);
    check(tube.second < 0.3f && in.minDe > 0.01f, "two sizes: enclosed, it stays small");

    printf("%s\n", failures ? "autopilot test FAILED" : "autopilot test passed");
    return failures ? 1 : 0;
}
