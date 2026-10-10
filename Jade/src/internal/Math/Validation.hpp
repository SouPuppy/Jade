#pragma once

#include <Jade/Color.hpp>
#include <Jade/Graphics/Camera.hpp>
#include <Jade/Math/Vector.hpp>

#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace Jade::Detail {

inline void Validate(Vec2 value)
{
  if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
    throw std::invalid_argument("Coordinates must be finite.");
  }
}

inline void Validate(Vec3 value)
{
  if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
    throw std::invalid_argument("Coordinates must be finite.");
  }
}

inline void Validate(Color value)
{
  for (float component : {value.red, value.green, value.blue, value.alpha}) {
    if (!std::isfinite(component) || component < 0.0f || component > 1.0f) {
      throw std::invalid_argument("Color components must be finite and between zero and one.");
    }
  }
}

inline void ValidatePositive(float value)
{
  if (!std::isfinite(value) || value <= 0.0f) {
    throw std::invalid_argument("Size must be positive and finite.");
  }
}

inline void ValidateClipPlanes(float nearPlane, float farPlane)
{
  ValidatePositive(nearPlane);
  ValidatePositive(farPlane);
  if (farPlane <= nearPlane) {
    throw std::invalid_argument("The far plane must be beyond the near plane.");
  }
}

inline void ValidateCameraFrame(Vec3 position, Vec3 target, Vec3 up)
{
  Validate(position);
  Validate(target);
  Validate(up);
  const double x = static_cast<double>(target.x) - position.x;
  const double y = static_cast<double>(target.y) - position.y;
  const double z = static_cast<double>(target.z) - position.z;
  const double cx = y * up.z - z * up.y;
  const double cy = z * up.x - x * up.z;
  const double cz = x * up.y - y * up.x;
  if ((x == 0.0 && y == 0.0 && z == 0.0)
      || (cx == 0.0 && cy == 0.0 && cz == 0.0)) {
    throw std::invalid_argument("Camera target must differ from position; up must not be parallel to its direction.");
  }
}

inline void ValidateCamera(const Camera& camera)
{
  if (camera.projection != Projection::Perspective && camera.projection != Projection::Orthographic) {
    throw std::invalid_argument("Unknown camera projection mode.");
  }
  if (!std::isfinite(camera.fovDegrees) || camera.fovDegrees <= 0 || camera.fovDegrees >= 180) {
    throw std::invalid_argument("Perspective FOV must be between zero and 180 degrees.");
  }
  ValidatePositive(camera.viewHeight);
  ValidateClipPlanes(camera.nearPlane, camera.farPlane);
}

} // namespace Jade::Detail
