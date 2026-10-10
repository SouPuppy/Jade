#include "Motion.hpp"

#include "../internal/Math/Matrix4.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Jade::Motion {
namespace {

void Move(Transform &transform, Vec3 direction, float distance)
{
  Detail::Validate(transform.position);
  if (!std::isfinite(distance)) {
    throw std::invalid_argument("Movement distance must be finite.");
  }
  const Vec3 next{static_cast<float>(static_cast<double>(transform.position.x) +
                                     static_cast<double>(direction.x) * distance),
                  static_cast<float>(static_cast<double>(transform.position.y) +
                                     static_cast<double>(direction.y) * distance),
                  static_cast<float>(static_cast<double>(transform.position.z) +
                                     static_cast<double>(direction.z) * distance)};
  Detail::Validate(next);
  transform.position = next;
}

void Rotate(Transform &transform, Vec3 localAxis, double degrees)
{
  const auto next =
      (transform.rotation.normalized() * Quaternion::angleAxis(localAxis, degrees)).normalized();
  transform.rotation = next;
}

Quaternion FromBasis(Vec3 right, Vec3 up, Vec3 back)
{
  // Basis vectors are matrix columns. Pick the largest diagonal branch near 180 degrees.
  const double trace = static_cast<double>(right.x) + up.y + back.z;
  Quaternion q;
  if (trace > 0) {
    const double s = 2 * std::sqrt(trace + 1);
    q = {static_cast<float>(s / 4), static_cast<float>((up.z - back.y) / s),
         static_cast<float>((back.x - right.z) / s), static_cast<float>((right.y - up.x) / s)};
  } else if (right.x > up.y && right.x > back.z) {
    const double s = 2 * std::sqrt(1.0 + right.x - up.y - back.z);
    q = {static_cast<float>((up.z - back.y) / s), static_cast<float>(s / 4),
         static_cast<float>((up.x + right.y) / s), static_cast<float>((back.x + right.z) / s)};
  } else if (up.y > back.z) {
    const double s = 2 * std::sqrt(1.0 + up.y - right.x - back.z);
    q = {static_cast<float>((back.x - right.z) / s), static_cast<float>((up.x + right.y) / s),
         static_cast<float>(s / 4), static_cast<float>((back.y + up.z) / s)};
  } else {
    const double s = 2 * std::sqrt(1.0 + back.z - right.x - up.y);
    q = {static_cast<float>((right.y - up.x) / s), static_cast<float>((back.x + right.z) / s),
         static_cast<float>((back.y + up.z) / s), static_cast<float>(s / 4)};
  }
  return q.normalized();
}

} // namespace

Vec3 getFront(const Transform &transform) { return transform.rotation.rotate({0, 0, -1}); }
Vec3 getRight(const Transform &transform) { return transform.rotation.rotate({1, 0, 0}); }
Vec3 getUp(const Transform &transform) { return transform.rotation.rotate({0, 1, 0}); }

void lookAt(Transform &transform, Vec3 target, Vec3 upDirection)
{
  Detail::ValidateCameraFrame(transform.position, target, upDirection);
  const auto forward = Detail::Normalize({static_cast<double>(target.x) - transform.position.x,
                                          static_cast<double>(target.y) - transform.position.y,
                                          static_cast<double>(target.z) - transform.position.z});
  const auto horizontal =
      Detail::Normalize(Detail::Cross(forward, {upDirection.x, upDirection.y, upDirection.z}));
  const auto vertical = Detail::Cross(horizontal, forward);
  transform.rotation = FromBasis(
      {static_cast<float>(horizontal[0]), static_cast<float>(horizontal[1]),
       static_cast<float>(horizontal[2])},
      {static_cast<float>(vertical[0]), static_cast<float>(vertical[1]), static_cast<float>(vertical[2])},
      {-static_cast<float>(forward[0]), -static_cast<float>(forward[1]), -static_cast<float>(forward[2])});
}

void forward(Transform &transform, float distance) { Move(transform, getFront(transform), distance); }
void backward(Transform &transform, float distance) { Move(transform, -getFront(transform), distance); }
void right(Transform &transform, float distance) { Move(transform, getRight(transform), distance); }
void left(Transform &transform, float distance) { Move(transform, -getRight(transform), distance); }
void up(Transform &transform, float distance) { Move(transform, {0, 1, 0}, distance); }
void down(Transform &transform, float distance) { Move(transform, {0, -1, 0}, distance); }
void yaw(Transform &transform, double degrees) { Rotate(transform, {0, 1, 0}, -degrees); }
void pitch(Transform &transform, double degrees) { Rotate(transform, {1, 0, 0}, degrees); }
void roll(Transform &transform, double degrees) { Rotate(transform, {0, 0, 1}, -degrees); }

void zoom(Camera &camera, double scroll)
{
  Detail::ValidateCamera(camera);
  if (!std::isfinite(scroll)) {
    throw std::invalid_argument("Zoom increment must be finite.");
  }
  if (scroll == 0) {
    return;
  }
  const double factor = std::exp(-std::clamp(scroll, -100.0, 100.0) * 0.1);
  if (camera.projection == Projection::Perspective) {
    // Scale the image-plane span, rather than the angle. Keep interactive zoom
    // away from the near-180-degree rectilinear projection singularity.
    constexpr double pi = 3.14159265358979323846;
    const double span = std::tan(camera.fovDegrees * pi / 360) * factor;
    camera.fovDegrees = static_cast<float>(std::clamp(2 * std::atan(span) * 180 / pi, 1.0, 120.0));
  } else {
    camera.viewHeight = static_cast<float>(std::clamp(camera.viewHeight * factor, 0.01, 10000.0));
  }
}

} // namespace Jade::Motion
