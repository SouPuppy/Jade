#pragma once

#include <Jade/Core/Transform.hpp>
#include <Jade/Math/Matrix4.hpp>
#include <Jade/Graphics/Camera.hpp>

#include "Validation.hpp"

#include <array>
#include <cstddef>

namespace Jade::Detail {

inline Matrix4 Checked(Matrix4 matrix)
{
  for (const auto value : matrix.values) {
    if (!std::isfinite(value)) {
      throw std::invalid_argument("Transform exceeds the supported floating-point range.");
    }
  }
  return matrix;
}

using Direction = std::array<double, 3>;
inline Direction Normalize(Direction value)
{
  const double length = std::hypot(value[0], value[1], value[2]);
  if (!std::isfinite(length) || length == 0) {
    throw std::invalid_argument("Direction must have nonzero finite length.");
  }
  return {value[0] / length, value[1] / length, value[2] / length};
}
inline Direction Cross(Direction a, Direction b)
{
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

inline Matrix4 RotationMatrix(Quaternion quaternion)
{
  const auto q = quaternion.normalized();
  const std::array<Vec3, 3> axes{q.rotate({1, 0, 0}), q.rotate({0, 1, 0}), q.rotate({0, 0, 1})};
  Matrix4 result = Matrix4::Identity();
  for (std::size_t column = 0; column < 3; ++column) {
    result.at(0, column) = axes[column].x;
    result.at(1, column) = axes[column].y;
    result.at(2, column) = axes[column].z;
  }
  return result;
}

inline Matrix4 ProjectionMatrix(const Camera& camera, double aspect)
{
  ValidateCamera(camera);
  if (!std::isfinite(aspect) || aspect <= 0) {
    throw std::invalid_argument("Camera aspect ratio must be positive and finite.");
  }
  const double nearPlane = camera.nearPlane, farPlane = camera.farPlane;
  Matrix4 result;
  if (camera.projection == Projection::Perspective) {
    constexpr double pi = 3.14159265358979323846;
    const double focal = 1.0 / std::tan(camera.fovDegrees * pi / 360);
    result.at(0, 0) = static_cast<float>(focal / aspect);
    result.at(1, 1) = static_cast<float>(focal);
    result.at(2, 2) = static_cast<float>(-(farPlane + nearPlane) / (farPlane - nearPlane));
    result.at(2, 3) = static_cast<float>(-2 * farPlane * nearPlane / (farPlane - nearPlane));
    result.at(3, 2) = -1;
  } else {
    const double height = camera.viewHeight;
    result.at(0, 0) = static_cast<float>(2 / (height * aspect));
    result.at(1, 1) = static_cast<float>(2 / height);
    result.at(2, 2) = static_cast<float>(-2 / (farPlane - nearPlane));
    result.at(2, 3) = static_cast<float>(-(farPlane + nearPlane) / (farPlane - nearPlane));
    result.at(3, 3) = 1;
  }
  return Checked(result);
}

// Camera ignores Transform scale; its view is the inverse rigid pose.
inline Matrix4 ViewMatrix(const Transform& pose)
{
  Validate(pose.position);
  Matrix4 translation = Matrix4::Identity();
  translation.at(0, 3) = -pose.position.x;
  translation.at(1, 3) = -pose.position.y;
  translation.at(2, 3) = -pose.position.z;
  return RotationMatrix(pose.rotation.normalized().conjugate()) * translation;
}

inline Matrix4 ModelMatrix(const Transform& transform)
{
  Validate(transform.position);
  ValidatePositive(transform.scale.x);
  ValidatePositive(transform.scale.y);
  ValidatePositive(transform.scale.z);
  Matrix4 translation = Matrix4::Identity();
  translation.at(0, 3) = transform.position.x;
  translation.at(1, 3) = transform.position.y;
  translation.at(2, 3) = transform.position.z;
  Matrix4 scaling = Matrix4::Identity();
  scaling.at(0, 0) = transform.scale.x;
  scaling.at(1, 1) = transform.scale.y;
  scaling.at(2, 2) = transform.scale.z;
  return translation * RotationMatrix(transform.rotation) * scaling;
}

inline Matrix4 NormalMatrix(const Transform& transform)
{
  ValidatePositive(transform.scale.x);
  ValidatePositive(transform.scale.y);
  ValidatePositive(transform.scale.z);
  Matrix4 inverseScale = Matrix4::Identity();
  inverseScale.at(0, 0) = 1 / transform.scale.x;
  inverseScale.at(1, 1) = 1 / transform.scale.y;
  inverseScale.at(2, 2) = 1 / transform.scale.z;
  return RotationMatrix(transform.rotation) * Checked(inverseScale);
}

} // namespace Jade::Detail
