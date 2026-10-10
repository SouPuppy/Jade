#pragma once

#include <Jade/Math/Matrix4.hpp>
#include <Jade/Math/Vector.hpp>
#include <Jade/Renderer/Common/LinearRGB.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Jade::Renderer::Math {

inline constexpr float Pi = 3.14159265358979323846f;
inline float length(Vec3 v) { return std::hypot(v.x, v.y, v.z); }
inline Vec3 unit(Vec3 v)
{
  const auto n = length(v);
  if (!std::isfinite(n) || n == 0)
    throw std::invalid_argument("Invalid ray or surface direction.");
  return v * (1 / n);
}
inline Vec3 abs(Vec3 v) { return {std::abs(v.x), std::abs(v.y), std::abs(v.z)}; }
inline Vec3 multiply(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3 point(const Matrix4 &m, Vec3 p)
{
  return {m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z + m.at(0, 3),
          m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z + m.at(1, 3),
          m.at(2, 0) * p.x + m.at(2, 1) * p.y + m.at(2, 2) * p.z + m.at(2, 3)};
}
inline Vec3 vector(const Matrix4 &m, Vec3 p)
{
  return {m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z,
          m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z,
          m.at(2, 0) * p.x + m.at(2, 1) * p.y + m.at(2, 2) * p.z};
}
inline Vec3 normal(const Matrix4 &inverse, Vec3 n)
{
  return unit({inverse.at(0, 0) * n.x + inverse.at(1, 0) * n.y + inverse.at(2, 0) * n.z,
               inverse.at(0, 1) * n.x + inverse.at(1, 1) * n.y + inverse.at(2, 1) * n.z,
               inverse.at(0, 2) * n.x + inverse.at(1, 2) * n.y + inverse.at(2, 2) * n.z});
}
inline Matrix4 inverseAffine(const Matrix4 &m)
{
  for (const auto value : m.values)
    if (!std::isfinite(value))
      throw std::invalid_argument("Geometry transform must be finite.");
  if (m.at(3, 0) != 0 || m.at(3, 1) != 0 || m.at(3, 2) != 0 || m.at(3, 3) != 1)
    throw std::invalid_argument("Geometry transform must be affine.");
  const double a = m.at(0, 0), b = m.at(0, 1), c = m.at(0, 2), d = m.at(1, 0), e = m.at(1, 1), f = m.at(1, 2),
               g = m.at(2, 0), h = m.at(2, 1), i = m.at(2, 2);
  const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (!std::isfinite(determinant) || determinant <= 0)
    throw std::invalid_argument(
        "Geometry transform must be finite and nonsingular with positive determinant.");
  Matrix4 result = Matrix4::Identity();
  const double inverse = 1 / determinant;
  const double values[3][3] = {{e * i - f * h, c * h - b * i, b * f - c * e},
                               {f * g - d * i, a * i - c * g, c * d - a * f},
                               {d * h - e * g, b * g - a * h, a * e - b * d}};
  for (std::size_t row = 0; row < 3; ++row)
    for (std::size_t col = 0; col < 3; ++col)
      result.at(row, col) = static_cast<float>(values[row][col] * inverse);
  const auto translation = vector(result, {m.at(0, 3), m.at(1, 3), m.at(2, 3)});
  result.at(0, 3) = -translation.x;
  result.at(1, 3) = -translation.y;
  result.at(2, 3) = -translation.z;
  for (auto v : result.values)
    if (!std::isfinite(v))
      throw std::invalid_argument("Geometry inverse exceeds floating-point range.");
  return result;
}
inline float powerHeuristic(float a, float b)
{
  const double aa = static_cast<double>(a) * a, bb = static_cast<double>(b) * b;
  return aa + bb > 0 ? static_cast<float>(aa / (aa + bb)) : 0;
}
inline void basis(Vec3 n, Vec3 &tangent, Vec3 &bitangent)
{
  tangent = unit(std::abs(n.z) < 0.999f ? cross({0, 0, 1}, n) : cross({0, 1, 0}, n));
  bitangent = cross(n, tangent);
}

} // namespace Jade::Renderer::Math
