#pragma once
#include <Jade/Math/Vector.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Jade::Detail {
struct PhysicsVector {
  double x = 0, y = 0, z = 0;
  double operator[](std::size_t i) const { return i == 0 ? x : i == 1 ? y : z; }
  double &operator[](std::size_t i) { return i == 0 ? x : i == 1 ? y : z; }
  Vec3 vector() const {
    constexpr double limit = std::numeric_limits<float>::max();
    for (const double value : {x, y, z}) {
      if (!std::isfinite(value) || std::abs(value) > limit)
        throw std::invalid_argument("Physics value exceeds the supported floating-point range.");
    }
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
  }
};
inline PhysicsVector operator+(PhysicsVector a, PhysicsVector b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline PhysicsVector operator-(PhysicsVector a, PhysicsVector b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline PhysicsVector operator-(PhysicsVector a) {
  return {-a.x, -a.y, -a.z};
}
inline PhysicsVector operator*(PhysicsVector a, double s) {
  return {a.x * s, a.y * s, a.z * s};
}
inline double collisionDot(PhysicsVector a, PhysicsVector b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline PhysicsVector collisionCross(PhysicsVector a, PhysicsVector b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double collisionLength(PhysicsVector a) {
  return std::hypot(a.x, a.y, a.z);
}
inline PhysicsVector collisionUnit(PhysicsVector a) {
  return a * (1 / collisionLength(a));
}

using CollisionVector = PhysicsVector;
} // namespace Jade::Detail
