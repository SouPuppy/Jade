#include "CollisionDebug.hpp"

#include "../../Physics/CollisionWorld.hpp"

#include <numbers>

namespace Jade::Detail {
namespace {

void boxLines(CollisionVector center, const std::array<CollisionVector, 3>& axes,
              CollisionVector half, std::vector<Vec3>& lines)
{
  std::array<Vec3, 8> corners;
  for (std::size_t i = 0; i < corners.size(); ++i) {
    auto point = center;
    for (std::size_t axis = 0; axis < 3; ++axis)
      point = point + axes[axis] * ((i & (std::size_t{1} << axis)) ? half[axis] : -half[axis]);
    corners[i] = point.vector();
  }
  for (std::size_t i = 0; i < corners.size(); ++i) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto bit = std::size_t{1} << axis;
      if (!(i & bit))
        lines.insert(lines.end(), {corners[i], corners[i | bit]});
    }
  }
}

} // namespace

CollisionDebugLines captureCollisionDebug(const Scene& scene, bool shapes, bool aabbs, bool contacts,
                                         const Physics::System* simulation)
{
  CollisionDebugLines result;
  if (!shapes && !aabbs && !contacts)
    return result;
  const std::array<CollisionVector, 3> basis{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  for (const auto& collider : captureColliders(scene)) {
    if (shapes) {
      if (collider.kind == CollisionKind::Box)
        boxLines(collider.center, collider.axes, collider.halfExtents, result.shapes);
      else {
        constexpr std::size_t segments = 64;
        for (std::size_t circle = 0; circle < 3; ++circle) {
          const auto u = basis[circle], v = basis[(circle + 1) % 3];
          for (std::size_t segment = 0; segment < segments; ++segment) {
            const auto point = [&](std::size_t index) {
              const double angle = 2 * std::numbers::pi * static_cast<double>(index) / segments;
              return (collider.center + (u * std::cos(angle) + v * std::sin(angle)) * collider.radius).vector();
            };
            result.shapes.insert(result.shapes.end(), {point(segment), point(segment + 1)});
          }
        }
      }
    }
    if (aabbs)
      boxLines((collider.bounds.minimum + collider.bounds.maximum) * 0.5, basis,
               (collider.bounds.maximum - collider.bounds.minimum) * 0.5, result.aabbs);
  }
  if (contacts) {
    result.contacts = simulation ? simulation->contactSnapshot(scene) : Physics::manifolds(scene);
    for (const auto& manifold : result.contacts) for (const auto& contact : manifold.points) {
      const auto center = (contact.pointA + contact.pointB) * 0.5f;
      constexpr std::size_t segments = 16;
      for (std::size_t circle = 0; circle < 3; ++circle) {
        const auto u = basis[circle].vector(), v = basis[(circle + 1) % 3].vector();
        for (std::size_t i = 0; i < segments; ++i) {
          const auto point = [&](std::size_t index) {
            const auto angle = 2 * std::numbers::pi * static_cast<double>(index) / segments;
            return center + (u * static_cast<float>(std::cos(angle)) + v * static_cast<float>(std::sin(angle))) * 0.025f;
          };
          result.points.insert(result.points.end(), {point(i), point(i + 1)});
        }
      }
      const auto n = manifold.normal, end = center + n * 0.25f;
      const auto seed = std::abs(n.x) < 0.7f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
      const auto side = collisionUnit(collisionCross({n.x,n.y,n.z}, {seed.x,seed.y,seed.z})).vector() * 0.04f;
      result.normals.insert(result.normals.end(), {center, end, end, end - n * 0.07f + side, end, end - n * 0.07f - side});
    }
  }
  return result;
}

} // namespace Jade::Detail
