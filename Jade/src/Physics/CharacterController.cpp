#include <Jade/Physics/CharacterController.hpp>
#include <Jade/Physics/System.hpp>
#include <Jade/Physics/World.hpp>
#include <Jade/Core/Transform.hpp>
#include "../internal/Physics/CollisionWorld.hpp"

namespace Jade {
Vec3 CharacterController::move(Jade::World& scene,Entity character,Vec3 displacement) const {
  if (!scene.owns(character) || !character.has<Transform>() || !character.has<Collider>() || character.has<RigidBody>() || scene.parent(character))
    throw std::invalid_argument("Character requires a root Transform and Collider without RigidBody.");
  if (!std::isfinite(displacement.x) || !std::isfinite(displacement.z) || !std::isfinite(groundHeight) || !std::isfinite(maxStepDistance) || maxStepDistance <= 0)
    throw std::invalid_argument("Invalid planar character movement.");
  if (std::hypot(displacement.x,displacement.z)/maxStepDistance > 10000) throw std::invalid_argument("Character displacement exceeds limit.");
  auto& pose = character.get<Transform>(); const auto start = pose.position;
  if (character.get<Collider>().empty()) { pose.position = start+Vec3{displacement.x,0,displacement.z}; pose.position.y = groundHeight; return pose.position-start; }
  auto& world = scene.physicsWorld(); Physics::System bridge(world.config()); bridge.synchronize(scene);
  const auto resolved = Detail::resolveCollider(character,scene.worldMatrix(character));
  Physics::BodyDescription d; d.position = {start.x,groundHeight,start.z}; d.rotation = pose.rotation;
  d.shape = resolved.kind == Detail::CollisionKind::Sphere ? Collider{Sphere{static_cast<float>(resolved.radius)}} : Collider{Box{resolved.halfExtents.vector()}};
  Physics::Vector remaining{displacement.x,0,displacement.z}; Physics::QueryFilter filter; filter.ignored = scene.physicsBody(character); filter.motions = 1;
  const auto bottom = resolved.bounds.minimum.y + groundHeight-start.y;
  const auto top = resolved.bounds.maximum.y + groundHeight-start.y;
  const auto blocksPlanarMovement = [&](const auto& hit) {
    if (std::abs(hit.normal.y) > .5) return false;
    const auto obstacle = world.description(hit.body);
    double extent;
    if (Detail::ColliderAccess::kind(obstacle.shape) == Detail::CollisionKind::Sphere)
      extent = Detail::ColliderAccess::sphere(obstacle.shape).radius;
    else {
      const auto half = Detail::ColliderAccess::box(obstacle.shape).halfExtents;
      extent = std::abs(obstacle.rotation.rotate({1,0,0}).y)*half.x
        + std::abs(obstacle.rotation.rotate({0,1,0}).y)*half.y
        + std::abs(obstacle.rotation.rotate({0,0,1}).y)*half.z;
    }
    // A neighbouring floor can report a side normal at its edge even though
    // the character only touches its top. Require actual vertical overlap.
    constexpr double tolerance = 1e-7;
    return obstacle.position.y+extent > bottom+tolerance && obstacle.position.y-extent < top-tolerance;
  };
  filter.acceptHit = [&](const auto& hit) { return blocksPlanarMovement(hit) && hit.penetration > 0; };
  for (unsigned iteration = 0; iteration < 8; ++iteration) {
    const auto hit = world.shapeCast(d,{},filter); if (!hit) break;
    Physics::Vector normal{hit->normal.x,0,hit->normal.z}; const auto length = Detail::collisionLength(normal);
    d.position = d.position+normal*((hit->penetration+1e-4)/(length*length));
  }
  filter.acceptHit = [&](const auto& hit) {
    return blocksPlanarMovement(hit) && Detail::collisionDot(remaining,hit.normal) < -1e-10;
  };
  for (unsigned iteration = 0; iteration < 8 && Detail::collisionLength(remaining) > 1e-8; ++iteration) {
    const auto hit = world.shapeCast(d,remaining,filter);
    if (!hit) { d.position = d.position+remaining; remaining = {}; break; }
    const auto length = Detail::collisionLength(remaining); const auto fraction = std::max(0.0,hit->fraction-1e-4/length);
    d.position = d.position+remaining*fraction; remaining = remaining*(1-fraction);
    Physics::Vector normal{hit->normal.x,0,hit->normal.z}; normal = normal*(1/Detail::collisionLength(normal));
    remaining = remaining-normal*std::min(0.0,Detail::collisionDot(remaining,normal));
  }
  const auto next = d.position.vector(); pose.position = next; return pose.position-start;
}
} // namespace Jade
