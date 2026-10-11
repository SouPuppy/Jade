#pragma once
#include "CollisionWorld.hpp"
#include <Jade/Physics/System.hpp>
namespace Jade::Detail {
std::optional<Physics::Manifold> collideManifold(const WorldCollider& a, const WorldCollider& b, double margin);
void updateBounds(WorldCollider& collider);
} // namespace Jade::Detail
