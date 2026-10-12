#pragma once
#include "CollisionWorld.hpp"
#include <Jade/Physics/System.hpp>
namespace Jade::Detail {
struct PreciseContactPoint {
  CollisionVector pointA, pointB;
  double separation = 0, normalImpulse = 0;
  CollisionVector frictionImpulse;
};
struct CollisionManifold {
  Entity a, b;
  CollisionVector normal;
  Physics::SmallList<PreciseContactPoint,8> points;
};
std::optional<CollisionManifold> collideManifoldPrecise(const WorldCollider& a, const WorldCollider& b, double margin);
Physics::Manifold presentation(const CollisionManifold& manifold);
std::optional<Physics::Manifold> collideManifold(const WorldCollider& a, const WorldCollider& b, double margin);
void updateBounds(WorldCollider& collider);
} // namespace Jade::Detail
