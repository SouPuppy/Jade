#pragma once

#include <Jade/Math/Vector.hpp>

namespace Jade {

// Hamilton product, right-handed rotation. Default is identity.
struct Quaternion {
  float w = 1;
  float x = 0;
  float y = 0;
  float z = 0;

  static Quaternion angleAxis(Vec3 axis, double degrees);
  Quaternion normalized() const;
  Quaternion conjugate() const noexcept { return {w, -x, -y, -z}; }
  Vec3 rotate(Vec3 vector) const;
};

Quaternion operator*(Quaternion a, Quaternion b);
constexpr bool operator==(Quaternion a, Quaternion b) noexcept
{
  return a.w == b.w && a.x == b.x && a.y == b.y && a.z == b.z;
}

} // namespace Jade
