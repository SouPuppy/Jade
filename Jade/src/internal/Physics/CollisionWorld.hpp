#pragma once

#include <Jade/Core/Scene.hpp>
#include <Jade/Physics/Collider.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Jade::Detail {

struct CollisionVector {
  double x = 0, y = 0, z = 0;
  double operator[](std::size_t i) const { return i == 0 ? x : i == 1 ? y : z; }
  double& operator[](std::size_t i) { return i == 0 ? x : i == 1 ? y : z; }
  Vec3 vector() const
  {
    constexpr double limit = std::numeric_limits<float>::max();
    for (const double value : {x, y, z}) {
      if (!std::isfinite(value) || std::abs(value) > limit)
        throw std::invalid_argument("Collision geometry exceeds the supported floating-point range.");
    }
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
  }
};
inline CollisionVector operator+(CollisionVector a, CollisionVector b)
{
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline CollisionVector operator-(CollisionVector a, CollisionVector b)
{
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline CollisionVector operator-(CollisionVector a) { return {-a.x, -a.y, -a.z}; }
inline CollisionVector operator*(CollisionVector a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double collisionDot(CollisionVector a, CollisionVector b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline CollisionVector collisionCross(CollisionVector a, CollisionVector b)
{
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double collisionLength(CollisionVector a) { return std::hypot(a.x, a.y, a.z); }
inline CollisionVector collisionUnit(CollisionVector a) { return a * (1 / collisionLength(a)); }

enum class CollisionKind { Box, Sphere };

struct ColliderAccess {
  static CollisionKind kind(const Collider& collider)
  {
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
