#pragma once
#include "CollisionWorld.hpp"
namespace Jade::Physics::Internal {
// Certified separating plane from support-mapped GJK distance. Returns a lower
// distance bound, which is safe for conservative advancement even on early exit.
struct DistancePlane { double distance; Vector normal; };
DistancePlane shapeDistance(const Detail::WorldCollider& a,const Detail::WorldCollider& b);
} // namespace Jade::Physics::Internal
