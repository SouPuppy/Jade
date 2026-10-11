#pragma once
#include <Jade/Math/Vector.hpp>

namespace Jade {
// SI units. Velocities are world-space; angularVelocity is radians/second.
// Zero mass is static. Empty Colliders suspend physics, preserving velocities.
// Participating dynamic bodies require root Transform and a nonempty Collider.
struct RigidBody {
  Vec3 velocity;
  Vec3 angularVelocity;
  float mass = 1;
  float friction = 0.6f;
  float restitution = 0;
};
} // namespace Jade
