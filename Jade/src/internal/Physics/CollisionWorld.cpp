#include "CollisionWorld.hpp"

#include "../Core/SceneTransforms.hpp"

namespace Jade::Detail {
namespace {

void positive(double value)
{
  if (!std::isfinite(value) || value <= 0)
    throw std::invalid_argument("Collider dimensions must be positive and finite.");
}

} // namespace

WorldCollider resolveCollider(Entity entity, const Matrix4& world)
{
  const auto& collider = entity.get<Collider>();
  const auto kind = ColliderAccess::kind(collider);
  constexpr double relativeTolerance = 1e-5;
  WorldCollider result;
  result.entity = entity;
  result.center = {world.at(0, 3), world.at(1, 3), world.at(2, 3)};
  std::array<CollisionVector, 3> columns;
  std::array<double, 3> scale;
  for (std::size_t i = 0; i < 3; ++i) {
    columns[i] = {world.at(0, i), world.at(1, i), world.at(2, i)};
    scale[i] = collisionLength(columns[i]);
    positive(scale[i]);
    result.axes[i] = columns[i] * (1 / scale[i]);
  }
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = i + 1; j < 3; ++j) {
      if (std::abs(collisionDot(result.axes[i], result.axes[j])) > relativeTolerance)
        throw std::invalid_argument("Collider world transform must not contain shear.");
    }
  }
  // Correct matrix rounding so all narrow-phase box axes are orthonormal.
  result.axes[1] = collisionUnit(result.axes[1] -
                                result.axes[0] * collisionDot(result.axes[0], result.axes[1]));
  result.axes[2] = collisionUnit(collisionCross(result.axes[0], result.axes[1]));

  result.kind = kind;
  CollisionVector extent;
  if (result.kind == CollisionKind::Sphere) {
    const auto radius = ColliderAccess::sphere(collider).radius;
    positive(radius);
    const double largest = std::max({scale[0], scale[1], scale[2]});
    if (largest - std::min({scale[0], scale[1], scale[2]}) > largest * relativeTolerance)
      throw std::invalid_argument("Sphere Collider requires uniform world scale.");
    result.radius = radius * ((scale[0] + scale[1] + scale[2]) / 3);
    extent = {result.radius, result.radius, result.radius};
  } else {
    const auto half = ColliderAccess::box(collider).halfExtents;
    const CollisionVector local{half.x, half.y, half.z};
    for (std::size_t i = 0; i < 3; ++i) {
      positive(local[i]);
      // Project the transformed local edges onto the corrected basis. This also
      // conservatively encloses rounding-sized departures from orthogonality.
      for (std::size_t j = 0; j < 3; ++j)
        result.halfExtents[i] += std::abs(collisionDot(result.axes[i], columns[j])) * local[j];
    }
    for (std::size_t i = 0; i < 3; ++i) {
      for (std::size_t j = 0; j < 3; ++j)
        extent[i] += std::abs(result.axes[j][i]) * result.halfExtents[j];
    }
  }
  result.bounds = {result.center - extent, result.center + extent};
  // The public contacts and debug vertices use float coordinates.
  (void)result.bounds.minimum.vector();
  (void)result.bounds.maximum.vector();
  return result;
}

std::vector<WorldCollider> captureColliders(const Scene& scene)
{
  SceneTransforms transforms(scene);
  std::vector<WorldCollider> result;
  for (const auto entity : scene.query<Collider, Transform>()) {
    if (entity.get<Collider>().empty()) continue;
    result.push_back(resolveCollider(entity, transforms.Resolve(entity).model));
  }
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.entity.id() < b.entity.id(); });
  return result;
}

} // namespace Jade::Detail
