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

    // Turn toward it at a limited rate - faster when something is in the way, and fast
    // enough to follow a surface curving at the scale of the clearance.
    float c = std::clamp(heading.dot(desired), -1.0f, 1.0f), ang = std::acos(c);
    float follow = 1.5f * speed / std::max(std::max(de, clearance), 1e-9f);
    float maxTurn = (std::max(0.45f, follow) + 2.2f * urgency) * dt;
    Vec3 nh = desired;
    if (ang > maxTurn && ang > 1e-6f) {
        Vec3 side = desired - heading * c;
        side = side.length() < 1e-6f ? anyPerpendicular(heading) : side.normalized();
        nh = heading * std::cos(maxTurn) + side * std::sin(maxTurn);
    }
    nh = limitClimb(nh, 0.85f);
    float yaw0 = std::atan2(heading.x, heading.z), yaw1 = std::atan2(nh.x, nh.z), dyaw = yaw1 - yaw0;
    if (dyaw > 3.14159265f) dyaw -= 6.2831853f;
    if (dyaw < -3.14159265f) dyaw += 6.2831853f;
    turnRate = dt > 0 ? dyaw / dt : 0;
    heading = nh;
    // lean into turns: turning right (yaw growing) lowers the right wing, a negative roll
    float bank = std::clamp(-turnRate * 0.6f, -0.55f, 0.55f);
    roll += (bank - roll) * (1 - std::exp(-dt * 2.5f));

    // speed: a few clearances per second, less in tight spots and when something's ahead
    float cruise = speedFactor * std::clamp(de, wall * 0.3f, wall * (style == FlightStyle::Around ? 1.5f : 3.0f));
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
        float open = std::clamp((misses / (float)s.dirs.size() - 0.2f) / 0.5f, 0.0f, 1.0f);
        look = limitClimb(heading - n * (lookIn * open), 0.85f);
        status = urgency > 0.5f ? "avoiding" : de < wall * 0.7f ? "threading" : "exploring";
    }
    gaze = limitClimb(gaze + (look - gaze) * (1 - std::exp(-dt * 2.5f)), 0.85f);
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
        log.de.push_back(d);
        log.minDe = std::min(log.minDe, d);
    }
    return log;
}
