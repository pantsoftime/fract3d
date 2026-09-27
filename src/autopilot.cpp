#include "autopilot.h"

#include <algorithm>
#include <deque>

static Vec3 anyPerpendicular(const Vec3& n) {
    Vec3 p = n.cross(Vec3(0, 1, 0));
    if (p.length() < 1e-3f) p = n.cross(Vec3(1, 0, 0));
    return p.normalized();
}

// Keeps a direction away from straight up or down (the camera's yaw is undefined there).
static Vec3 limitClimb(Vec3 d, float maxY) {
    d = d.normalized();
    if (std::abs(d.y) <= maxY) return d;
    float h = std::sqrt(d.x * d.x + d.z * d.z);
    float s = std::sqrt(1 - maxY * maxY) / std::max(h, 1e-6f);
    if (h < 1e-6f) return Vec3(std::sqrt(1 - maxY * maxY), std::copysign(maxY, d.y), 0);
    return Vec3(d.x * s, std::copysign(maxY, d.y), d.z * s);
}

void Autopilot::engage(FlightStyle s, const Vec3& look, float clearanceTarget, unsigned seed) {
    active = true;
    style = s;
    heading = limitClimb(look, 0.8f);
    gaze = look.normalized();
    wantF = heading;
    omega = Vec3(0, 0, 0);
    gazeTarget = gaze;
    gazeYaw = std::atan2(gaze.x, gaze.z);
    gazePitch = std::asin(std::clamp(gaze.y, -1.0f, 1.0f));
    gazeYawV = gazePitchV = 0;
    openF = 0;
    normalFValid = false;
    clearance = std::max(clearanceTarget, 1e-7f);
    wallDistance = clearance;
    speed = 0;
    time = 0;
    turnRate = 0;
    roomAhead = 0;
    for (int i = 0; i < 6; i++) {  // a different wander on every flight
        seed = seed * 1664525u + 1013904223u;
        phase[i] = (float)(seed >> 8) / (float)(1u << 24) * 6.2831853f;
    }
    status = "engaged";
}

// Around sees a few clearances out; Through sees a few times the room it's in (so a
// small chamber looks as roomy to it as a large one).
float Autopilot::whiskerRange(float de) const {
    if (style == FlightStyle::Around) return std::max(de, clearance * 0.5f) * 5.0f;
    return std::max(de, wallDistance * 0.5f) * 7.0f;
}

// Rings of rays around the heading: straight ahead, then 20, 40, 65, 95 and 135 degrees
// off it, and one straight back (for turning around in a dead end).
std::vector<Vec3> Autopilot::whiskerDirs() const {
    Vec3 f = heading.normalized(), r = anyPerpendicular(f), u = f.cross(r);
    std::vector<Vec3> d;
    d.push_back(f);
    const float rings[6] = {20, 40, 65, 95, 135, 180};
    const int counts[6] = {6, 8, 8, 6, 4, 1};
    for (int k = 0; k < 6; k++) {
        float a = rings[k] * 0.0174533f, ca = std::cos(a), sa = std::sin(a);
        for (int j = 0; j < counts[k]; j++) {
            float b = (j + 0.5f * (k & 1)) * 6.2831853f / counts[k];
            d.push_back((f * ca + (r * std::cos(b) + u * std::sin(b)) * sa).normalized());
        }
    }
    return d;
}

Vec3 Autopilot::step(float dt, const Vec3& pos, const ShipSensors& s, Vec3& look) {
    time += dt;
    look = heading;
    if (!s.valid || s.dirs.empty() || s.dirs.size() != s.free.size()) {
        speed *= std::exp(-dt * 4);
        status = "waiting for sensors";
        return pos;
    }
    const bool nOk = s.normalValid && std::isfinite(s.normal.length()) && s.normal.length() > 0;
    Vec3 n = nOk ? s.normal.normalized() : Vec3(0, 1, 0);
    if (nOk) {  // (filtered over about a fifth of a second: bumpy walls make it jump from frame to frame)
        Vec3 m = normalFValid ? normalF + (n - normalF) * (1 - std::exp(-dt / 0.2f)) : n;
        normalF = m.length() > 0.2f ? m.normalized() : n;  // (a flip to another wall: take it)
        normalFValid = true;
        n = normalF;
    }
    // The nearest surface can't be farther than the free distance along any whisker: where
    // the distance estimate runs long (height fields near cliffs), believe the whiskers.
    float de = s.de;
    for (float f : s.free) de = std::min(de, std::max(f, 0.0f));
    if (de <= s.eps) {
        // Touching (a view can start inside the structure): step out along the surface's
        // normal by the smallest distance that is still resolvable here - never more.
        speed = 0;
        status = "backing out";
        // (no usable normal - a flat or broken estimate there: reverse the way it came)
        Vec3 away = nOk ? n : heading * -1.0f;
        return s.eps > 0 ? pos + away * (s.eps * 0.5f) : pos;
    }
    float L = std::max(s.range, 1e-9f);
    float obj = s.objectDe > 0 ? s.objectDe : de;  // how far the fractal itself is (the floor aside)

    // How much room there is straight ahead, and which way has the most room (the
    // score prefers small turns, so the ship doesn't dither between equal openings).
    float ahead = L;
    int best = -1, misses = 0;
    float bestScore = -1;
    Vec3 flow(0, 0, 0);
    for (size_t i = 0; i < s.dirs.size(); i++) {
        float room = std::min(std::max(s.free[i], 0.0f), L) / L;
        float align = s.dirs[i].dot(heading);
        if (align > 0.9f) ahead = std::min(ahead, std::max(s.free[i], 0.0f));
        if (s.hit.size() == s.free.size() ? !s.hit[i] : s.free[i] >= L * 0.999f) misses++;
        float score = room * room * (0.55f + 0.45f * align);
        float w = score * score * score * score;
        flow += s.dirs[i] * w;
        if (score > bestScore) bestScore = score, best = (int)i;
    }
    roomAhead = ahead;
    float urgency = std::clamp(1.0f - ahead / (0.6f * L), 0.0f, 1.0f);
    // Through keeps a distance from the walls that suits the room it's in (a third of the
    // typical free distance around it), up to the clearance it was given.
    float wall = clearance;
    if (style == FlightStyle::Through) {
        std::vector<float> f = s.free;
        std::nth_element(f.begin(), f.begin() + f.size() / 2, f.end());
        float lo = std::min(std::max(s.eps * 4, clearance * 1e-4f), clearance);  // (clearance can be below float precision here)
        wall = std::clamp(0.35f * f[f.size() / 2], lo, clearance);
    }
    wallDistance = wall;
    // anything closer than that pushes the ship away (the floor too, which the normal ignores)
    Vec3 push(0, 0, 0);
    for (size_t i = 0; i < s.dirs.size(); i++)
        if (s.free[i] < wall) {
            float k = 1.0f - std::max(s.free[i], 0.0f) / wall;
            push += s.dirs[i] * (-k * k);
        }

    Vec3 desired;
    float climb = 0;  // Around: + when too low
    if (style == FlightStyle::Around) {
        climb = std::clamp((clearance - obj) / clearance, -1.0f, 1.0f);
        Vec3 tan = heading - n * heading.dot(n);
        tan = tan.length() < 0.2f ? anyPerpendicular(n) : tan.normalized();
        desired = tan + n * (climb * 1.2f);
        if (orbitCenter && orbitRadius > 0) {
            Vec3 h(pos.x - center.x, 0, pos.z - center.z);
            float r = h.length();
            if (r > orbitRadius * 1e-3f) {
                Vec3 radial = h * (1.0f / r), around = Vec3(0, 1, 0).cross(radial);
                if (around.dot(heading) < 0) around = around * -1.0f;  // keep going the way it's going
                desired = desired + around * 0.7f - radial * (0.8f * std::clamp((r - orbitRadius) / orbitRadius, -1.0f, 1.0f));
            }
        }
    } else {
        desired = flow.length() > 1e-12f ? flow.normalized() : heading;
        float nearWall = wall * 0.7f;
        if (de < nearWall) desired = desired + n * (1.5f * (1.0f - de / nearWall));  // too close: ease off
        // Hold the wall distance, as Around holds its height: out in the open "the roomiest
        // way" points away from the structure, so this brings it back to the surface - and
        // along the surface, the roomiest way leads into the openings it passes.
        desired = desired + n * (0.8f * std::clamp((wall - obj) / wall, -1.0f, 1.0f));
    }
    desired = desired.normalized() + push * 1.5f;
    if (best >= 0 && urgency > 0) desired = desired.normalized() * (1 - urgency) + s.dirs[best] * (1.5f * urgency);
    // a slow wander, so no two flights are the same and it doesn't circle one spot forever
    const float* ph = phase;
    Vec3 w(std::sin(time * 0.11f + ph[0]) + 0.6f * std::sin(time * 0.27f + ph[1]),
           0.5f * (std::sin(time * 0.07f + ph[2]) + 0.6f * std::sin(time * 0.19f + ph[3])),
           std::sin(time * 0.13f + ph[4]) + 0.6f * std::sin(time * 0.23f + ph[5]));
    desired = limitClimb(desired.normalized() + w * (0.18f * (1 - urgency)), 0.8f);

    // Steer like a vehicle, not a pointer. The wanted direction is recomputed every frame
    // from noisy probes, so it is filtered first (quicker when something is in the way); the
    // heading then turns toward it with an angular velocity that can only change so fast,
    // so turns ease in and out instead of snapping. (Unfiltered, the ship turned at 90
    // degrees a second in calm flight through the Kleinian caves.)
    float tauWant = 0.3f * (1 - 0.7f * urgency);
    Vec3 wf = wantF + (desired - wantF) * (1 - std::exp(-dt / std::max(tauWant, 0.02f)));
    wantF = wf.length() > 0.1f ? wf.normalized() : desired;
    float c = std::clamp(heading.dot(wantF), -1.0f, 1.0f), ang = std::acos(c);
    Vec3 axis = heading.cross(wantF);
    axis = axis.length() > 1e-6f ? axis.normalized() : (ang > 1.0f ? anyPerpendicular(heading) : Vec3(0, 0, 0));
    // limits: turning fast enough to follow a surface curving at the scale of the clearance
    // (Around) and to dodge what's ahead, but no faster
    float follow = 0.9f * speed / std::max(std::max(de, clearance * 0.5f), 1e-9f);
    float wMax = std::clamp(follow, 0.5f, 1.0f) + 1.4f * urgency;  // radians per second (1: 57 degrees)
    float aMax = 1.8f + 6.0f * urgency;                             // radians per second squared
    // the turn rate that still stops at the target without overshooting it
    float wWant = std::min(wMax, std::sqrt(2.0f * aMax * ang));
    Vec3 dw = axis * wWant - omega;
    float dwMax = aMax * dt;
    if (dw.length() > dwMax) dw = dw * (dwMax / dw.length());
    omega += dw;
    float rate = omega.length();
    Vec3 nh = heading;
    if (rate * dt > 1e-7f) {  // rotate the heading about omega (Rodrigues)
        Vec3 k = omega * (1.0f / rate);
        float th = rate * dt, ct = std::cos(th), st = std::sin(th);
        nh = heading * ct + k.cross(heading) * st + k * (k.dot(heading) * (1 - ct));
    }
    nh = limitClimb(nh, 0.85f);
    turnRate = omega.dot(Vec3(0, 1, 0));  // yaw rate: positive turns toward +x from +z, i.e. right
    heading = nh;
    // lean into turns - gently: turning right lowers the right wing (a negative roll)
    float bank = std::clamp(-turnRate * 0.35f, -0.4f, 0.4f);
    roll += (bank - roll) * (1 - std::exp(-dt * 1.5f));

    // speed: a few clearances per second, less in tight spots, when something's ahead, and
    // when a sharp turn is needed (as a pilot slows for a hairpin rather than whipping round)
    float cruise = speedFactor * std::clamp(de, wall * 0.3f, wall * (style == FlightStyle::Around ? 1.5f : 3.0f));
    cruise *= 0.35f + 0.65f * std::max(heading.dot(wantF), 0.0f);
    cruise = std::max(std::min(cruise, ahead / 1.2f), speedFactor * wall * 0.05f);
    speed += (cruise - speed) * (1 - std::exp(-dt * 1.5f));
    float stepLen = std::min(speed * dt, 0.5f * de);  // never beyond the free sphere around the ship

    if (style == FlightStyle::Around) {
        // look in toward the surface (about 50 degrees for an object: it fills the lower part
        // of the view with its outline against the sky, like a shot from orbit)
        look = heading - n * lookIn;
        if (orbitCenter && orbitRadius > 0) {  // circling: look in across the middle, like a sightseeing flight
            Vec3 h(center.x - pos.x, 0, center.z - pos.z);
            if (h.length() > 1e-9f) look = look.normalized() + h.normalized() * 0.9f;
        }
        look = limitClimb(look, 0.85f);
        status = climb > 0.3f ? "climbing" : climb < -0.3f ? "descending" : urgency > 0.5f ? "avoiding" : "cruising";
    } else {
        // In a tunnel it looks where it's going; skimming an outside wall (much of the
        // space around it open) it turns its gaze toward the wall, as Around does.
        // (filtered: a single probe ray seeing through a gap made the gaze jolt)
        float open = std::clamp((misses / (float)s.dirs.size() - 0.2f) / 0.5f, 0.0f, 1.0f);
        openF += (open - openF) * (1 - std::exp(-dt / 0.5f));
        look = limitClimb(heading - n * (lookIn * openF), 0.85f);
        status = urgency > 0.5f ? "avoiding" : de < wall * 0.7f ? "threading" : "exploring";
    }
    // The camera's gaze follows on a critically damped spring rather than a simple lag: a
    // lag's velocity jumps the moment its target does, a spring's only accelerates, so a
    // sudden change of where to look becomes a smooth S-curve instead of a jolt.
    // Where to look can itself jump (the normal flipping to the opposite wall of a narrow
    // passage): the spring's target only moves toward it at up to 60 degrees a second. The
    // limit on climbing is applied to the target, so the gaze eases into it (clamping the
    // gaze itself stopped it dead: the worst jolts were exactly there).
    look = limitClimb(look, 0.8f);
    {
        float c2 = std::clamp(gazeTarget.dot(look), -1.0f, 1.0f), a2 = std::acos(c2), step2 = 1.05f * dt;
        if (a2 > step2) {
            Vec3 side = look - gazeTarget * c2;
            side = side.length() < 1e-6f ? anyPerpendicular(gazeTarget) : side.normalized();
            gazeTarget = gazeTarget * std::cos(step2) + side * std::sin(step2);
        } else {
            gazeTarget = look;
        }
    }
    // The spring runs on the gaze's yaw and pitch separately: on a single angle a critically
    // damped spring never overshoots its target, so the pitch stays within the target's limit
    // (a spring on the direction vector could climb past it while the target swung sideways,
    // and a limit on the gaze then stopped it dead - the last jolts were exactly there).
    const float w0 = 3.0f;  // radians per second: settles in about a second
    float tYaw = std::atan2(gazeTarget.x, gazeTarget.z), tPitch = std::asin(std::clamp(gazeTarget.y, -1.0f, 1.0f));
    float dYaw = std::remainder(tYaw - gazeYaw, 6.2831853f);  // (the short way round)
    gazeYawV += (dYaw * (w0 * w0) - gazeYawV * (2.0f * w0)) * dt;
    gazePitchV += ((tPitch - gazePitch) * (w0 * w0) - gazePitchV * (2.0f * w0)) * dt;
    gazeYaw = std::remainder(gazeYaw + gazeYawV * dt, 6.2831853f);
    gazePitch += gazePitchV * dt;
    float cp = std::cos(gazePitch);
    gaze = Vec3(std::sin(gazeYaw) * cp, std::sin(gazePitch), std::cos(gazeYaw) * cp);
    look = gaze;
    return pos + heading * stepLen;
}

// ------------------------------------------------------------------ CPU flight (tests)
FlightLog simulateFlight(const std::function<float(const Vec3&)>& de, Autopilot ap, Vec3 pos, float seconds, float dt) {
    FlightLog log;
    struct Pending {
        ShipSensors s;
        Vec3 at;
    };
    std::deque<Pending> q;  // the GPU's results arrive a couple of frames late: so do these
    auto sense = [&](const Vec3& p) {
        ShipSensors s;
        s.valid = true;
        s.de = de(p);
        float h = std::max(std::abs(s.de) * 0.1f, 1e-4f);
        Vec3 g(de(p + Vec3(h, 0, 0)) - de(p - Vec3(h, 0, 0)), de(p + Vec3(0, h, 0)) - de(p - Vec3(0, h, 0)),
               de(p + Vec3(0, 0, h)) - de(p - Vec3(0, 0, h)));
        s.normalValid = g.length() > 0;
        s.normal = g.normalized();
        s.eps = 1e-5f;
        s.dirs = ap.whiskerDirs();
        s.range = ap.whiskerRange(s.de);
        for (auto& d : s.dirs) {
            float t = 0, hit = s.range;
            for (int i = 0; i < 300; i++) {
                float r = de(p + d * t);
                if (r < std::max(1e-5f, t * 1e-3f)) {
                    hit = t;
                    break;
                }
                t += r;
                if (t > s.range) break;
            }
            s.free.push_back(hit);
            s.hit.push_back(hit < s.range);
        }
        return s;
    };
    for (float t = 0; t < seconds; t += dt) {
        q.push_back({sense(pos), pos});
        ShipSensors s;
        if (q.size() > 2) {  // corrected for the distance flown since, exactly as App::flyAutopilot does
            s = q.front().s;
            float moved = (pos - q.front().at).length();
            if (s.de > s.eps) {
                s.de -= moved;
                for (auto& f : s.free) f = std::max(f - moved, 0.0f);
                if (s.de <= s.eps) s.valid = false;
            }
            q.pop_front();
        }
        Vec3 look;
        Vec3 np = ap.step(dt, pos, s, look);
        log.travelled += (np - pos).length();
        pos = np;
        float d = de(pos);
        log.pos.push_back(pos);
        log.look.push_back(look);
        log.de.push_back(d);
        log.minDe = std::min(log.minDe, d);
    }
    return log;
}
