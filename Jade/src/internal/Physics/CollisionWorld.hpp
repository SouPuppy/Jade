#pragma once

#include <Jade/Core/Scene.hpp>
#include <Jade/Physics/Collider.hpp>
#include "Math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Jade::Detail {

enum class CollisionKind { Box, Sphere };

struct ColliderAccess {
  static CollisionKind kind(const Collider& collider)
  {
    if (collider.empty()) throw std::invalid_argument("An empty Collider has no collision shape.");
    return collider.m_Kind == Collider::Kind::Box ? CollisionKind::Box : CollisionKind::Sphere;
  }
  static Box box(const Collider& collider) { return collider.m_Shape.box; }
  static Sphere sphere(const Collider& collider) { return collider.m_Shape.sphere; }
};

struct CollisionAabb {
  CollisionVector minimum, maximum;
  bool overlaps(const CollisionAabb& other, double tolerance) const
  {
    for (std::size_t i = 0; i < 3; ++i) {
      if (maximum[i] + tolerance < other.minimum[i] || other.maximum[i] + tolerance < minimum[i])
        return false;
    }
    return true;
  }
};

struct WorldCollider {
  Entity entity;
  CollisionKind kind;
  CollisionVector center;
  std::array<CollisionVector, 3> axes;
  CollisionVector halfExtents;
  double radius = 0;
  CollisionAabb bounds;
  double size() const
  {
    return kind == CollisionKind::Sphere ? radius : std::max({halfExtents.x, halfExtents.y, halfExtents.z});
  }
};

WorldCollider resolveCollider(Entity entity, const Matrix4& world);
std::vector<WorldCollider> captureColliders(const Scene& scene);

} // namespace Jade::Detail
