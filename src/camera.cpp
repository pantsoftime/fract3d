#include "camera.h"

#include <algorithm>

static constexpr float kMaxPitch = 1.55f;

Vec3 Camera::forward() const {
    float cp = std::cos(pitch);
    return {std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp};
}
Vec3 Camera::right() const { return Vec3(0, 1, 0).cross(forward()).normalized(); }
Vec3 Camera::up() const { return forward().cross(right()).normalized(); }

void Camera::lookAt(const Vec3& from, const Vec3& to) {
    Vec3 d = to - from;
    distance = std::max(d.length(), 1e-6f);
    Vec3 f = d * (1.0f / distance);
    pitch = std::asin(std::clamp(f.y, -1.0f, 1.0f));
    yaw = std::atan2(f.x, f.z);
    pos = from;
}

void Camera::orbit(float dYaw, float dPitch) {
    Vec3 t = target();
    yaw += dYaw;
    pitch = std::clamp(pitch + dPitch, -kMaxPitch, kMaxPitch);
    pos = t - forward() * distance;
}

void Camera::look(float dYaw, float dPitch) {
    yaw += dYaw;
    pitch = std::clamp(pitch + dPitch, -kMaxPitch, kMaxPitch);
}

void Camera::pan(float dx, float dy) {
    pos += right() * (dx * distance) + up() * (dy * distance);
}

void Camera::dolly(float factor) {
    Vec3 t = target();
    distance = std::max(distance * factor, 1e-7f);
    pos = t - forward() * distance;
}

void Camera::move(const Vec3& d, float amount) {
    pos += (right() * d.x + up() * d.y + forward() * d.z) * amount;
}

void Camera::setTargetDistance(float d) { distance = std::max(d, 1e-7f); }
