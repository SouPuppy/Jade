#pragma once
#include <Jade/Math/Vector.hpp>

namespace Jade {
// SI units. Velocities are world-space; angularVelocity is radians/second.
// Zero mass is static. Dynamic bodies require root Transform and Collider.
struct RigidBody {
  Vec3 velocity;
  Vec3 angularVelocity;
  float mass = 1;
  float friction = 0.6f;
  float restitution = 0;
};
} // namespace Jade
