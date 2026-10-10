#pragma once

#include <Jade/Objects/Camera.hpp>
#include <Jade/Objects/Object.hpp>

#include "Validation.hpp"

#include <array>
#include <cstddef>

namespace Jade::Detail {

// Column-major storage for glUniformMatrix4fv(..., GL_FALSE, ...).
struct Matrix4 {
  std::array<float, 16> values{};

  float& at(std::size_t row, std::size_t column) noexcept { return values[column * 4 + row]; }
  float at(std::size_t row, std::size_t column) const noexcept { return values[column * 4 + row]; }
  const float* data() const noexcept { return values.data(); }

  static Matrix4 Identity() noexcept
  {
    Matrix4 result;
    for (std::size_t index = 0; index < 4; ++index) {
      result.at(index, index) = 1.0f;
    }
    return result;
  }
};

inline Matrix4 Checked(Matrix4 matrix)
{
  for (float value : matrix.values) {
    if (!std::isfinite(value)) {
      throw std::invalid_argument("Transform exceeds the supported floating-point range.");
    }
  }
  return matrix;
}

inline Matrix4 operator*(const Matrix4& a, const Matrix4& b)
{
  Matrix4 result;
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      double value = 0;
      for (std::size_t index = 0; index < 4; ++index) {
        value += static_cast<double>(a.at(row, index)) * b.at(index, column);
      }
      result.at(row, column) = static_cast<float>(value);
    }
  }
  return Checked(result);
}

inline Matrix4 OverlayMatrix(double width, double height)
{
  Matrix4 result = Matrix4::Identity();
  result.at(0, 0) = static_cast<float>(2.0 / width);
  result.at(1, 1) = static_cast<float>(2.0 / height);
  result.at(0, 3) = -1;
  result.at(1, 3) = -1;
  return Checked(result);
}

inline Matrix4 ProjectionMatrix(const Camera& camera, double aspect)
{
  if (!std::isfinite(aspect) || aspect <= 0) {
    throw std::invalid_argument("Camera aspect ratio must be positive and finite.");
  }
  const double nearPlane = camera.NearPlane();
  const double farPlane = camera.FarPlane();
  Matrix4 result;
  if (camera.Mode() == Projection::Perspective) {
    constexpr double pi = 3.14159265358979323846;
    const double focal = 1.0 / std::tan(camera.FOV() * pi / 360.0);
    result.at(0, 0) = static_cast<float>(focal / aspect);
    result.at(1, 1) = static_cast<float>(focal);
    result.at(2, 2) = static_cast<float>(-(farPlane + nearPlane) / (farPlane - nearPlane));
    result.at(2, 3) = static_cast<float>(-2 * farPlane * nearPlane / (farPlane - nearPlane));
    result.at(3, 2) = -1;
  } else {
    const double height = camera.ViewHeight();
    result.at(0, 0) = static_cast<float>(2.0 / (height * aspect));
    result.at(1, 1) = static_cast<float>(2.0 / height);
    result.at(2, 2) = static_cast<float>(-2.0 / (farPlane - nearPlane));
    result.at(2, 3) = static_cast<float>(-(farPlane + nearPlane) / (farPlane - nearPlane));
    result.at(3, 3) = 1;
  }
  return Checked(result);
}

using Direction = std::array<double, 3>;

inline Direction Normalize(Direction value)
{
  const double length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
  if (!std::isfinite(length) || length == 0) {
    throw std::invalid_argument("Camera direction must have nonzero finite length.");
  }
  return {value[0] / length, value[1] / length, value[2] / length};
}

inline Direction Cross(Direction a, Direction b)
{
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

inline std::array<Matrix4, 3> RotationAxes(Vec3 rotation)
{
  constexpr double radians = 3.14159265358979323846 / 180.0;
  const double x = std::remainder(static_cast<double>(rotation.x), 360.0) * radians;
  const double y = std::remainder(static_cast<double>(rotation.y), 360.0) * radians;
  const double z = std::remainder(static_cast<double>(rotation.z), 360.0) * radians;
  Matrix4 rx = Matrix4::Identity();
  rx.at(1, 1) = rx.at(2, 2) = static_cast<float>(std::cos(x));
  rx.at(2, 1) = static_cast<float>(std::sin(x));
  rx.at(1, 2) = -rx.at(2, 1);
  Matrix4 ry = Matrix4::Identity();
  ry.at(0, 0) = ry.at(2, 2) = static_cast<float>(std::cos(y));
  ry.at(0, 2) = static_cast<float>(std::sin(y));
  ry.at(2, 0) = -ry.at(0, 2);
  Matrix4 rz = Matrix4::Identity();
  rz.at(0, 0) = rz.at(1, 1) = static_cast<float>(std::cos(z));
  rz.at(1, 0) = static_cast<float>(std::sin(z));
  rz.at(0, 1) = -rz.at(1, 0);
  return {rx, ry, rz};
}

inline Matrix4 RotationMatrix(Vec3 rotation)
{
  const auto axes = RotationAxes(rotation);
  return axes[2] * axes[1] * axes[0];
}

inline Matrix4 CameraRotationMatrix(Vec3 rotation)
{
  const auto axes = RotationAxes(rotation);
  // Roll affects right/up around the current view, without changing front.
  return axes[1] * axes[0] * axes[2];
}

inline Matrix4 ViewMatrix(const Camera& camera)
{
  const auto eye = camera.Position();
  const auto target = camera.Target();
  const auto up = camera.Up();
  const auto forward = Normalize({static_cast<double>(target.x) - eye.x,
    static_cast<double>(target.y) - eye.y, static_cast<double>(target.z) - eye.z});
  const auto right = Normalize(Cross(forward, {up.x, up.y, up.z}));
  const auto vertical = Cross(right, forward);
  Matrix4 result = Matrix4::Identity();
  const Direction position{eye.x, eye.y, eye.z};
  for (std::size_t index = 0; index < 3; ++index) {
    result.at(0, index) = static_cast<float>(right[index]);
    result.at(1, index) = static_cast<float>(vertical[index]);
    result.at(2, index) = static_cast<float>(-forward[index]);
  }
  result.at(0, 3) = static_cast<float>(-(right[0] * position[0] + right[1] * position[1] + right[2] * position[2]));
  result.at(1, 3) = static_cast<float>(-(vertical[0] * position[0] + vertical[1] * position[1] + vertical[2] * position[2]));
  result.at(2, 3) = static_cast<float>(forward[0] * position[0] + forward[1] * position[1] + forward[2] * position[2]);
  // Invert the camera's scale and local Euler offset before the look-at view.
  const auto scale = camera.Scale();
  Matrix4 inverseScale = Matrix4::Identity();
  inverseScale.at(0, 0) = 1.0f / scale.x;
  inverseScale.at(1, 1) = 1.0f / scale.y;
  inverseScale.at(2, 2) = 1.0f / scale.z;
  const auto rotation = CameraRotationMatrix(camera.Rotation());
  Matrix4 inverseRotation = Matrix4::Identity();
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      inverseRotation.at(row, column) = rotation.at(column, row);
    }
  }
  return Checked(inverseScale) * inverseRotation * Checked(result);
}

inline Matrix4 ModelMatrix(const Object& object)
{
  const auto position = object.Position();
  const auto scale = object.Scale();
  Matrix4 translation = Matrix4::Identity();
  translation.at(0, 3) = position.x;
  translation.at(1, 3) = position.y;
  translation.at(2, 3) = position.z;
  Matrix4 scaling = Matrix4::Identity();
  scaling.at(0, 0) = scale.x;
  scaling.at(1, 1) = scale.y;
  scaling.at(2, 2) = scale.z;
  return translation * RotationMatrix(object.Rotation()) * Checked(scaling);
}

// Inverse transpose of the model's linear part. Translation cannot affect normals.
inline Matrix4 NormalMatrix(const Object& object)
{
  const auto scale = object.Scale();
  Matrix4 inverseScale = Matrix4::Identity();
  inverseScale.at(0, 0) = 1.0f / scale.x;
  inverseScale.at(1, 1) = 1.0f / scale.y;
  inverseScale.at(2, 2) = 1.0f / scale.z;
  return RotationMatrix(object.Rotation()) * Checked(inverseScale);
}

} // namespace Jade::Detail
