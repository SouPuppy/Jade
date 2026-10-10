#include "Quaternion.hpp"

#include "../internal/Math/Validation.hpp"

#include <cmath>
#include <stdexcept>

namespace Jade {
namespace {

float CheckedFloat(double value)
{
  const float result = static_cast<float>(value);
  if (!std::isfinite(result)) {
    throw std::invalid_argument("Rotation exceeds the supported floating-point range.");
  }
  return result;
}

} // namespace

Quaternion Quaternion::normalized() const
{
  if (!std::isfinite(w) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
    throw std::invalid_argument("Quaternion components must be finite.");
  }
  const double length = std::sqrt(static_cast<double>(w) * w + static_cast<double>(x) * x
    + static_cast<double>(y) * y + static_cast<double>(z) * z);
  if (length == 0) {
    throw std::invalid_argument("Quaternion must have nonzero length.");
  }
  return {static_cast<float>(w / length), static_cast<float>(x / length),
    static_cast<float>(y / length), static_cast<float>(z / length)};
}

Quaternion Quaternion::angleAxis(Vec3 axis, double degrees)
{
  Detail::Validate(axis);
  if (!std::isfinite(degrees)) {
    throw std::invalid_argument("Rotation increment must be finite.");
  }
  const double length = std::hypot(static_cast<double>(axis.x), axis.y, axis.z);
  if (length == 0) {
    throw std::invalid_argument("Rotation axis must have nonzero length.");
  }
  constexpr double radians = 3.14159265358979323846 / 180;
  const double halfAngle = std::remainder(degrees, 360.0) * radians / 2;
  const double sine = std::sin(halfAngle) / length;
  return Quaternion{static_cast<float>(std::cos(halfAngle)), static_cast<float>(axis.x * sine),
    static_cast<float>(axis.y * sine), static_cast<float>(axis.z * sine)}.normalized();
}

Quaternion operator*(Quaternion a, Quaternion b)
{
  return {
    CheckedFloat(static_cast<double>(a.w) * b.w - static_cast<double>(a.x) * b.x
      - static_cast<double>(a.y) * b.y - static_cast<double>(a.z) * b.z),
    CheckedFloat(static_cast<double>(a.w) * b.x + static_cast<double>(a.x) * b.w
      + static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y),
    CheckedFloat(static_cast<double>(a.w) * b.y - static_cast<double>(a.x) * b.z
      + static_cast<double>(a.y) * b.w + static_cast<double>(a.z) * b.x),
    CheckedFloat(static_cast<double>(a.w) * b.z + static_cast<double>(a.x) * b.y
      - static_cast<double>(a.y) * b.x + static_cast<double>(a.z) * b.w)
  };
}

Vec3 Quaternion::rotate(Vec3 vector) const
{
  Detail::Validate(vector);
  const auto q = normalized();
  const double xx = static_cast<double>(q.x) * q.x, yy = static_cast<double>(q.y) * q.y;
  const double zz = static_cast<double>(q.z) * q.z, xy = static_cast<double>(q.x) * q.y;
  const double xz = static_cast<double>(q.x) * q.z, yz = static_cast<double>(q.y) * q.z;
  const double wx = static_cast<double>(q.w) * q.x, wy = static_cast<double>(q.w) * q.y;
  const double wz = static_cast<double>(q.w) * q.z;
  return {
    CheckedFloat((1 - 2 * (yy + zz)) * vector.x + 2 * (xy - wz) * vector.y + 2 * (xz + wy) * vector.z),
    CheckedFloat(2 * (xy + wz) * vector.x + (1 - 2 * (xx + zz)) * vector.y + 2 * (yz - wx) * vector.z),
    CheckedFloat(2 * (xz - wy) * vector.x + 2 * (yz + wx) * vector.y + (1 - 2 * (xx + yy)) * vector.z)
  };
}

} // namespace Jade
