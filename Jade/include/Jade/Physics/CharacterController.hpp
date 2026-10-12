#pragma once
#include <Jade/World/World.hpp>
#include <Jade/Math/Vector.hpp>

namespace Jade {
// Root entity with Transform + Collider. Planar movement against static Colliders.
// Ground support is a fixed height; no gravity, steps, slopes or rigid-body pushing.
struct CharacterController {
  float groundHeight = 0;
  float maxStepDistance = 0.025f;
  Vec3 move(Jade::World& scene, Entity character, Vec3 displacement) const;
};
} // namespace Jade
