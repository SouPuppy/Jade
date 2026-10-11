#pragma once
#include <Jade/Math/Vector.hpp>
#include <Jade/Physics/Types.hpp>

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
  Physics::MotionType motion = Physics::MotionType::Dynamic;
  Physics::CollisionMode collision = Physics::CollisionMode::Discrete;
  bool allowSleep = true;
  float linearDamping = 0, angularDamping = 0;
  std::uint32_t category = 1, mask = ~std::uint32_t{0};
};
} // namespace Jade
