#pragma once
#include <cmath>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const { float l = length(); return l > 0 ? *this * (1.0f / l) : Vec3(0, 0, 1); }
    const float* data() const { return &x; }
};

// Yaw/pitch camera that always has an orbit target in front of it at `distance`.
//  - orbit():  rotate around the target
//  - look():   rotate in place (target moves)
//  - move():   fly; target moves with the camera
//  - dolly():  change distance to target, exponentially
struct Camera {
    Vec3 pos{0, 0, -3};
    float yaw = 0.0f;    // radians, 0 = looking down +z
    float pitch = 0.0f;  // radians, + = looking up
    float distance = 3.0f;
    float fovDeg = 55.0f;
    float roll = 0.0f;   // radians, banks the view (+ raises the right wing); the autopilot leans into turns; not saved

    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;
    Vec3 target() const { return pos + forward() * distance; }

    void lookAt(const Vec3& from, const Vec3& to);
    void orbit(float dYaw, float dPitch);
    void look(float dYaw, float dPitch);
    void pan(float dx, float dy);  // in units of distance
    void dolly(float factor);      // distance *= factor, keeping target fixed
    void move(const Vec3& localDir, float amount);
    void setTargetDistance(float d);  // keeps position, moves target along view
};
