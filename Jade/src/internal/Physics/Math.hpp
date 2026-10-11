#pragma once
#include <Jade/Math/Vector.hpp>
#include <Jade/Physics/Types.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Jade::Detail {
using PhysicsVector = Physics::Vector;
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
