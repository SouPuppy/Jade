#include <Jade/Graphics/CameraMotion.hpp>
#include <Jade/Graphics/Motion.hpp>
#include "../internal/Math/Validation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace Jade::CameraMotion {
namespace {
constexpr double degreesPerRadian = 180 / std::numbers::pi;
struct Angles { double yaw, pitch; };

Angles Heading(const Transform& pose)
{
  const auto front = Motion::getFront(pose), right = Motion::getRight(pose);
  const double yaw = std::hypot(front.x, front.z) > 1e-6
    ? std::atan2(front.x, -front.z) : std::atan2(right.z, right.x);
  return {yaw * degreesPerRadian,
    std::asin(std::clamp(static_cast<double>(front.y), -1.0, 1.0)) * degreesPerRadian};
}

Quaternion Rotation(Angles angles)
{
  return (Quaternion::angleAxis({0, 1, 0}, -std::remainder(angles.yaw, 360.0)) *
          Quaternion::angleAxis({1, 0, 0}, std::clamp(angles.pitch, -89.0, 89.0))).normalized();
}

void Translate(Transform& pose, Vec3 delta)
{
  Detail::Validate(pose.position);
  const auto next = pose.position + delta;
  Detail::Validate(next);
  pose.position = next;
}
}

void constrain(Transform& pose) { pose.rotation = Rotation(Heading(pose)); }

void look(Transform& pose, double yawDegrees, double pitchDegrees)
{
  if (!std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees))
    throw std::invalid_argument("Camera look angles must be finite.");
  auto angles = Heading(pose);
  angles.yaw += std::remainder(yawDegrees, 360.0);
  angles.pitch += std::clamp(pitchDegrees, -180.0, 180.0);
  pose.rotation = Rotation(angles);
}

void move(Transform& pose, Vec3 input, float distance)
{
  Detail::Validate(input);
  if (!std::isfinite(distance)) throw std::invalid_argument("Camera distance must be finite.");
  const double length = std::hypot(static_cast<double>(input.x), input.y, input.z);
  if (length == 0 || distance == 0) return;
  input = input * static_cast<float>(1 / std::max(1.0, length));
  const double yaw = Heading(pose).yaw / degreesPerRadian;
  const Vec3 front{static_cast<float>(std::sin(yaw)), 0, static_cast<float>(-std::cos(yaw))};
  const Vec3 right{static_cast<float>(std::cos(yaw)), 0, static_cast<float>(std::sin(yaw))};
  Translate(pose, (right * input.x + Vec3{0, input.y, 0} + front * input.z) * distance);
}

void pan(Transform& pose, const Camera& lens, double dx, double dy, int windowHeight, float distance)
{
  Detail::ValidateCamera(lens);
  if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(distance) || distance <= 0)
    throw std::invalid_argument("Camera pan deltas and depth must be finite with positive depth.");
  if (windowHeight <= 0 || (dx == 0 && dy == 0)) return;
  const double span = lens.projection == Projection::Orthographic ? lens.viewHeight
    : 2 * distance * std::tan(lens.fovDegrees / degreesPerRadian / 2);
  const double units = span / windowHeight;
  Translate(pose, Motion::getRight(pose) * static_cast<float>(-dx * units) +
                  Motion::getUp(pose) * static_cast<float>(dy * units));
}
} // namespace Jade::CameraMotion
