#include "Collision.hpp"

#include "../internal/Core/WorldTransforms.hpp"
#include "../internal/Physics/CollisionWorld.hpp"
#include "../internal/Physics/BroadPhase.hpp"

#include <limits>
#include <utility>

namespace Jade::Physics {
namespace {

using Detail::CollisionVector;
using Detail::WorldCollider;
using Detail::CollisionKind;
using Detail::collisionDot;
using Detail::collisionCross;
using Detail::collisionLength;

struct Hit {
  CollisionVector point, normal;
  double penetration;
};

std::optional<Hit> sphereSphere(const WorldCollider& a, const WorldCollider& b, double tolerance)
{
  const auto delta = b.center - a.center;
  const double distance = collisionLength(delta);
  const double depth = a.radius + b.radius - distance;
  if (depth < -tolerance)
    return std::nullopt;
  const auto normal = distance > 0 ? delta * (1 / distance) : CollisionVector{1, 0, 0};
  const auto first = a.center + normal * a.radius;
  const auto second = b.center - normal * b.radius;
  return Hit{(first + second) * 0.5, normal, std::max(0.0, depth)};
}

std::optional<Hit> sphereBox(const WorldCollider& sphere, const WorldCollider& box, double tolerance)
{
  const auto delta = sphere.center - box.center;
  CollisionVector local, towardBox;
  bool inside = true;
  for (std::size_t i = 0; i < 3; ++i) {
    local[i] = collisionDot(delta, box.axes[i]);
    inside = inside && std::abs(local[i]) <= box.halfExtents[i];
    towardBox = towardBox + box.axes[i] *
                             (std::clamp(local[i], -box.halfExtents[i], box.halfExtents[i]) - local[i]);
  }
  CollisionVector normal, closest;
  double depth;
  if (inside) {
    std::size_t face = 0;
    double distance = box.halfExtents[0] - std::abs(local[0]);
    for (std::size_t i = 1; i < 3; ++i) {
      const double next = box.halfExtents[i] - std::abs(local[i]);
      if (next < distance) {
        face = i;
        distance = next;
      }
    }
    const auto outward = box.axes[face] * (local[face] >= 0 ? 1.0 : -1.0);
    closest = sphere.center + outward * distance;
    // Moving the BOX opposite the nearest exit lets the sphere leave through it.
    normal = -outward;
    depth = sphere.radius + distance;
  } else {
    // Preserve the displacement in box coordinates: absolute witness points
    // can round together at large world origins even when this vector is nonzero.
    const double distance = collisionLength(towardBox);
    depth = sphere.radius - distance;
    if (depth < -tolerance)
      return std::nullopt;
    normal = towardBox * (1 / distance);
    closest = sphere.center + towardBox;
  }
  return Hit{(sphere.center + normal * sphere.radius + closest) * 0.5,
             normal, std::max(0.0, depth)};
}

double projectionRadius(const WorldCollider& box, CollisionVector axis)
{
  double result = 0;
  for (std::size_t i = 0; i < 3; ++i)
    result += box.halfExtents[i] * std::abs(collisionDot(box.axes[i], axis));
  return result;
}

// Clip an incident face against one side plane of a reference face.
std::vector<CollisionVector> clip(const std::vector<CollisionVector>& polygon,
                                  CollisionVector center, CollisionVector normal,
                                  double extent, double tolerance)
{
  std::vector<CollisionVector> result;
  if (polygon.empty())
    return result;
  auto previous = polygon.back();
  double previousDistance = collisionDot(previous - center, normal) - extent;
  for (const auto current : polygon) {
    const double distance = collisionDot(current - center, normal) - extent;
    const bool previousInside = previousDistance <= tolerance, currentInside = distance <= tolerance;
    if (previousInside != currentInside) {
      const double fraction = std::clamp(previousDistance / (previousDistance - distance), 0.0, 1.0);
      result.push_back(previous + (current - previous) * fraction);
    }
    if (currentInside)
      result.push_back(current);
    previous = current;
    previousDistance = distance;
  }
  return result;
}

CollisionVector support(const WorldCollider& box, CollisionVector normal)
{
  auto point = box.center;
  for (std::size_t i = 0; i < 3; ++i)
    point = point + box.axes[i] * (collisionDot(box.axes[i], normal) >= 0 ? box.halfExtents[i] : -box.halfExtents[i]);
  return point;
}

CollisionVector facePoint(const WorldCollider& reference, const WorldCollider& incident,
                          std::size_t face, CollisionVector normal, double tolerance)
{
  const auto center = reference.center + normal * reference.halfExtents[face];
  std::size_t incidentFace = 0;
  for (std::size_t i = 1; i < 3; ++i) {
    if (std::abs(collisionDot(incident.axes[i], normal)) >
        std::abs(collisionDot(incident.axes[incidentFace], normal)))
      incidentFace = i;
  }
  const double sign = collisionDot(incident.axes[incidentFace], normal) >= 0 ? -1.0 : 1.0;
  const auto incidentCenter = incident.center + incident.axes[incidentFace] * (sign * incident.halfExtents[incidentFace]);
  const std::size_t u = (incidentFace + 1) % 3, v = (incidentFace + 2) % 3;
  const auto edgeU = incident.axes[u] * incident.halfExtents[u], edgeV = incident.axes[v] * incident.halfExtents[v];
  std::vector<CollisionVector> polygon{incidentCenter - edgeU - edgeV, incidentCenter + edgeU - edgeV,
                                      incidentCenter + edgeU + edgeV, incidentCenter - edgeU + edgeV};
  for (std::size_t i = 0; i < 3; ++i) {
    if (i != face) {
      polygon = clip(polygon, center, reference.axes[i], reference.halfExtents[i], tolerance);
      polygon = clip(polygon, center, -reference.axes[i], reference.halfExtents[i], tolerance);
    }
  }
  CollisionVector sum;
  std::size_t count = 0;
  for (const auto point : polygon) {
    const double distance = collisionDot(point - center, normal);
    if (distance <= tolerance) {
      sum = sum + point - normal * (distance * 0.5);
      ++count;
    }
  }
  if (count)
    return sum * (1.0 / static_cast<double>(count));
  // A tolerance-only contact can have no exact clipped intersection. Keep a
  // finite surface-witness midpoint even in that numerically separated case.
  return (support(reference, normal) + support(incident, -normal)) * 0.5;
}

CollisionVector edgePoint(const WorldCollider& a, const WorldCollider& b,
                          std::size_t i, std::size_t j, CollisionVector normal)
{
  auto first = a.center, second = b.center;
  for (std::size_t k = 0; k < 3; ++k) {
    if (k != i)
      first = first + a.axes[k] * (collisionDot(a.axes[k], normal) >= 0 ? a.halfExtents[k] : -a.halfExtents[k]);
    if (k != j)
      second = second + b.axes[k] * (collisionDot(b.axes[k], normal) <= 0 ? b.halfExtents[k] : -b.halfExtents[k]);
  }
  const auto delta = first - second;
  const double cosine = collisionDot(a.axes[i], b.axes[j]);
  const double d = collisionDot(a.axes[i], delta), e = collisionDot(b.axes[j], delta);
  const double denominator = std::max(0.0, 1 - cosine * cosine);
  double s = denominator > 1e-12 ? (cosine * e - d) / denominator : 0;
  s = std::clamp(s, -a.halfExtents[i], a.halfExtents[i]);
  const double t = std::clamp(e + cosine * s, -b.halfExtents[j], b.halfExtents[j]);
  s = std::clamp(cosine * t - d, -a.halfExtents[i], a.halfExtents[i]);
  return (first + a.axes[i] * s + second + b.axes[j] * t) * 0.5;
}

std::optional<Hit> boxBox(const WorldCollider& a, const WorldCollider& b, double tolerance)
{
  const auto delta = b.center - a.center;
  double minimum = std::numeric_limits<double>::infinity();
  CollisionVector normal;
  // Face axes precede edge axes to make degenerate/tied contacts deterministic.
  std::size_t selected = 0;
  const auto test = [&](CollisionVector axis, std::size_t feature) {
    const double length = collisionLength(axis);
    if (length <= 1e-6)
      return true; // Parallel-edge cross products have no separating direction.
    axis = axis * (1 / length);
    const double centerProjection = collisionDot(delta, axis);
    const double depth = projectionRadius(a, axis) + projectionRadius(b, axis) - std::abs(centerProjection);
    if (depth < -tolerance)
      return false;
    if (depth < minimum) {
      minimum = depth;
      normal = centerProjection < 0 ? -axis : axis;
      selected = feature;
    }
    return true;
  };
  for (std::size_t i = 0; i < 3; ++i) {
    if (!test(a.axes[i], i))
      return std::nullopt;
  }
  for (std::size_t i = 0; i < 3; ++i) {
    if (!test(b.axes[i], 3 + i))
      return std::nullopt;
  }
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      if (!test(collisionCross(a.axes[i], b.axes[j]), 6 + i * 3 + j))
        return std::nullopt;
    }
  }
  CollisionVector point;
  if (selected < 3)
    point = facePoint(a, b, selected, normal, tolerance);
  else if (selected < 6)
    point = facePoint(b, a, selected - 3, -normal, tolerance);
  else
    point = edgePoint(a, b, (selected - 6) / 3, (selected - 6) % 3, normal);
  return Hit{point, normal, std::max(0.0, minimum)};
}

std::optional<Contact> query(const WorldCollider& a, const WorldCollider& b)
{
  const double tolerance = 1e-6 * std::max({1.0, a.size(), b.size()});
  if (!a.bounds.overlaps(b.bounds, tolerance))
    return std::nullopt;
  std::optional<Hit> hit;
  if (a.kind == CollisionKind::Sphere && b.kind == CollisionKind::Sphere)
    hit = sphereSphere(a, b, tolerance);
  else if (a.kind == CollisionKind::Box && b.kind == CollisionKind::Box)
    hit = boxBox(a, b, tolerance);
  else if (a.kind == CollisionKind::Sphere)
    hit = sphereBox(a, b, tolerance);
  else {
    hit = sphereBox(b, a, tolerance);
    if (hit)
      hit->normal = -hit->normal;
  }
  if (!hit)
    return std::nullopt;
  if (hit->penetration > std::numeric_limits<float>::max())
    throw std::invalid_argument("Collision penetration exceeds the supported floating-point range.");
  return Contact{a.entity, b.entity, hit->point.vector(), hit->normal.vector(), static_cast<float>(hit->penetration)};
}

} // namespace

std::optional<Contact> contact(const Jade::World& scene, Entity a, Entity b)
{
  if (!scene.owns(a) || !scene.owns(b))
    throw std::invalid_argument("Collision queries require live entities in the supplied Scene.");
  if (a == b || !a.has<Collider>() || !b.has<Collider>() || !a.has<Transform>() || !b.has<Transform>())
    return std::nullopt;
  if (a.get<Collider>().empty() || b.get<Collider>().empty()) return std::nullopt;
  const bool reversed = a.id() > b.id();
  if (reversed)
    std::swap(a, b);
  Detail::WorldTransforms transforms(scene);
  const auto first = Detail::resolveCollider(a, transforms.Resolve(a).model);
  const auto second = Detail::resolveCollider(b, transforms.Resolve(b).model);
  auto result = query(first, second);
  if (result && reversed) {
    std::swap(result->a, result->b);
    result->normal = -result->normal;
  }
  return result;
}

std::vector<Contact> detect(const Jade::World& scene)
{
  const auto colliders = Detail::captureColliders(scene);
  std::vector<Contact> result;
  double margin = 1e-6;
  for (const auto& collider : colliders) margin = std::max(margin, 1e-6 * collider.size());
  for (const auto pair : Detail::broadPhase(colliders, margin))
    if (auto hit = query(colliders[pair.a], colliders[pair.b])) result.push_back(*hit);
  return result;
}

} // namespace Jade::Physics
