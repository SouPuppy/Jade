#pragma once

#include <Jade/Math/Vector.hpp>

#include <vector>
#include <Jade/Physics/System.hpp>

namespace Jade { class World; }

namespace Jade::Detail {

// Presentation-only data, kept out of path tracing snapshots and revisions.
struct CollisionDebugLines {
  std::vector<Vec3> shapes, aabbs, points, normals;
  std::vector<Vec3> fieldForces, totalForces;
  std::vector<Physics::Manifold> contacts;
  std::size_t totalContactPoints = 0, shownContactPoints = 0;
};

CollisionDebugLines captureCollisionDebug(const World& scene, bool shapes, bool aabbs, bool contacts = false,
                                         const Physics::System* simulation = nullptr, bool forces = false,
                                         std::size_t maxContactMarkers = 4096);

} // namespace Jade::Detail
