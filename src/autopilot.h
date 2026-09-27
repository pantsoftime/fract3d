#pragma once
// autopilot.h — flies the 3D camera like a small spaceship, around a fractal or through it.
//
// The ship steers by what it can sense, the way a bat or a rover does: the distance
// estimate at the ship (how far the nearest surface is), its gradient (which way is
// "up" from that surface), and a few dozen "whiskers" - rays marched out in a cone
// around the heading, each reporting how much free space there is in its direction.
// On the GPU these come from the probe pass (probe.frag); the self-test feeds it
// analytic shapes instead.
//
//  - Around: holds a height above the surface and follows it sideways, so it circles
//    the object like a satellite (the Mandelbulb, a Julia set).
//  - Through: heads for the roomiest opening in front of it while keeping its distance
//    from the walls, so it wanders into tunnels and chambers (the Mandelbox, Kleinian
//    caves) - and turns around in dead ends.
//
// Safety comes first and doesn't depend on the steering: the ship never moves farther
// in a frame than half the distance to the nearest surface, so it cannot pass through
// one, however it turns.
#include <functional>
#include <vector>

#include "camera.h"

enum class FlightStyle { Around = 0, Through = 1 };

struct ShipSensors {
    bool valid = false;
    float de = -1;                 // distance to the nearest surface, floor included (a lower bound): for safety
    float objectDe = -1;           // distance to the fractal itself: the height Around holds; < 0: use de
    Vec3 normal{0, 1, 0};          // away from the fractal's nearest surface
    bool normalValid = false;
    std::vector<Vec3> dirs;        // whisker directions (world, unit)
    std::vector<float> free;       // free distance along each; range when nothing was hit
    std::vector<char> hit;         // whether each found a surface at all (empty: judge by free < range)
    float range = 0;               // how far the whiskers reached
    float eps = 0;                 // the finest distance resolvable at the ship: closer than this, it is touching
};

struct Autopilot {
    bool active = false;
    FlightStyle style = FlightStyle::Around;
    float clearance = 1.0f;    // Around: height to hold; Through: distance from the walls to aim for
    float speedFactor = 1.0f;  // cruise speed, in clearances per second
    float lookIn = 1.2f;       // Around: how far the gaze turns toward the surface (1.2: about 50 degrees; terrain wants less)
    bool orbitCenter = false;  // Around: also circle `center` at `orbitRadius` (horizontally) - for terrain
    Vec3 center;
    float orbitRadius = 0;
    Vec3 heading{0, 0, 1};
    Vec3 gaze{0, 0, 1};         // where the camera looks (eased, so the view drifts rather than twitches)
    float speed = 0;           // world units per second (smoothed)
    float roll = 0;            // bank angle for the camera (radians; Camera::roll: + raises the right wing)
    float time = 0;            // seconds engaged
    float phase[6] = {0.3f, 1.7f, 2.9f, 4.1f, 5.3f, 0.9f};  // wander: different on every engagement
    // for the dashboard
    float roomAhead = 0;       // free distance straight ahead
    float wallDistance = 0;    // the clearance actually kept (Through adapts it to the room it's in)
    float turnRate = 0;        // radians per second
    const char* status = "";

    void engage(FlightStyle s, const Vec3& look, float clearanceTarget, unsigned seed);
    float whiskerRange(float de) const;
    std::vector<Vec3> whiskerDirs() const;  // for the next probe (world directions)
    // Advances the ship by dt from `pos`; returns the new position. `look` receives the
    // camera's view direction (Around looks partly toward the object).
    Vec3 step(float dt, const Vec3& pos, const ShipSensors& s, Vec3& look);
};

// Flies the autopilot through an analytic scene on the CPU (sensors from the distance
// function by sphere tracing), for tests. Records every position and the clearance.
struct FlightLog {
    std::vector<Vec3> pos;
    std::vector<float> de;
    float minDe = 1e30f;
    float travelled = 0;
};
FlightLog simulateFlight(const std::function<float(const Vec3&)>& de, Autopilot ap, Vec3 start, float seconds, float dt);
