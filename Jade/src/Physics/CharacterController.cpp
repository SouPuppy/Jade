#include <Jade/Physics/CharacterController.hpp>
#include <Jade/Core/Transform.hpp>
#include <Jade/Physics/Collider.hpp>
#include <Jade/Physics/Collision.hpp>
#include <Jade/Physics/RigidBody.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Jade {
Vec3 CharacterController::move(Scene& scene, Entity character, Vec3 displacement) const {
  if (!std::isfinite(displacement.x) || !std::isfinite(displacement.z) || !std::isfinite(groundHeight) ||
      !std::isfinite(maxStepDistance) || maxStepDistance <= 0)
    throw std::invalid_argument("Invalid planar character movement.");
  if (!character.has<Transform>() || !character.has<Collider>() || character.has<RigidBody>())
    throw std::invalid_argument("Character requires Transform and Collider, without RigidBody.");
  if (scene.parent(character)) throw std::invalid_argument("Planar character must be a root entity.");
  auto& pose = character.get<Transform>();
  const auto start = pose.position;
  displacement.y = 0;
  const double count = std::max(1.0, std::ceil(static_cast<double>(std::hypot(displacement.x, displacement.z)) / maxStepDistance));
  if (count > 10000) throw std::invalid_argument("Character displacement exceeds the substep limit.");
  const auto steps = static_cast<unsigned>(count);
  const auto step = displacement * (1.0f / steps);
  const auto blockers = scene.query<Transform, Collider>();
  for (unsigned i = 0; i < steps; ++i) {
    const auto before = pose.position;
    pose.position = pose.position + step;
    pose.position.y = groundHeight;
    bool resolved = false;
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
      bool penetration = false;
      for (auto blocker : blockers) {
        if (blocker == character || blocker.has<RigidBody>()) continue;
        const auto hit = Physics::contact(scene, blocker, character);
        if (!hit || hit->penetration <= 0 || std::abs(hit->normal.y) > 0.5f) continue;
        penetration = true;
        pose.position = pose.position + Vec3{hit->normal.x, 0, hit->normal.z} * (hit->penetration + 0.0001f);
      }
      if (!penetration) { resolved = true; break; }
    }
    if (!resolved) pose.position = before;
  }
  return pose.position - start;
}
} // namespace Jade
