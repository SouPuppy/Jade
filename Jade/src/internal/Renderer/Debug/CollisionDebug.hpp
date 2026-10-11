#pragma once

#include <Jade/Math/Vector.hpp>

#include <vector>

namespace Jade { class Scene; }

namespace Jade::Detail {

// Presentation-only data, kept out of path tracing snapshots and revisions.
struct CollisionDebugLines {
  std::vector<Vec3> shapes, aabbs;
};

CollisionDebugLines captureCollisionDebug(const Scene& scene, bool shapes, bool aabbs);

} // namespace Jade::Detail
