#include "Manifold.hpp"

namespace Jade::Detail {
namespace {
using V = CollisionVector;
double radius(const WorldCollider& box, V axis) {
  double r = 0;
  for (std::size_t i = 0; i < 3; ++i) r += box.halfExtents[i] * std::abs(collisionDot(axis, box.axes[i]));
  return r;
}
V support(const WorldCollider& box, V direction) {
  V p = box.center;
  for (std::size_t i = 0; i < 3; ++i)
    p = p + box.axes[i] * (collisionDot(direction, box.axes[i]) >= 0 ? box.halfExtents[i] : -box.halfExtents[i]);
  return p;
}
std::vector<V> clip(const std::vector<V>& in, V center, V axis, double extent) {
  std::vector<V> out;
  if (in.empty()) return out;
  auto previous = in.back();
  double pd = collisionDot(previous - center, axis) - extent;
  for (const auto p : in) {
    const double d = collisionDot(p - center, axis) - extent;
    if ((d <= 0) != (pd <= 0)) out.push_back(previous + (p - previous) * (pd / (pd - d)));
    if (d <= 0) out.push_back(p);
    previous = p; pd = d;
  }
  return out;
}
Physics::ContactPoint point(V a, V b, V normal) {
  return {a.vector(), b.vector(), V{collisionDot(b - a, normal), 0, 0}.vector().x};
}
std::optional<Physics::Manifold> boxes(const WorldCollider& a, const WorldCollider& b, double margin) {
  const auto delta = b.center - a.center;
  double best = -std::numeric_limits<double>::infinity();
  V normal;
  std::size_t feature = 0;
  const auto test = [&](V axis, std::size_t id) {
    const double length = collisionLength(axis);
    if (length < 1e-6) return true;
    axis = axis * (1 / length);
    const double c = collisionDot(delta, axis);
    const double separation = std::abs(c) - radius(a, axis) - radius(b, axis);
    if (separation > margin) return false;
    // Keep a face manifold unless an edge is a materially better separating axis.
    if (separation > best + (id >= 6 ? 1e-4 : 1e-8)) {
      best = separation; normal = c < 0 ? -axis : axis; feature = id;
    }
    return true;
  };
  for (std::size_t i = 0; i < 3; ++i) if (!test(a.axes[i], i)) return std::nullopt;
  for (std::size_t i = 0; i < 3; ++i) if (!test(b.axes[i], i + 3)) return std::nullopt;
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      if (!test(collisionCross(a.axes[i], b.axes[j]), 6 + i * 3 + j)) return std::nullopt;
  Physics::Manifold result{a.entity, b.entity, normal.vector(), {}};
  if (feature < 6) {
    const bool reverse = feature >= 3;
    const auto& ref = reverse ? b : a;
    const auto& inc = reverse ? a : b;
    const auto n = reverse ? -normal : normal;
    const auto face = feature % 3;
    const auto center = ref.center + n * ref.halfExtents[face];
    std::size_t f = 0;
    for (std::size_t i = 1; i < 3; ++i)
      if (std::abs(collisionDot(inc.axes[i], n)) > std::abs(collisionDot(inc.axes[f], n))) f = i;
    const auto c = inc.center + inc.axes[f] * (collisionDot(inc.axes[f], n) >= 0 ? -inc.halfExtents[f] : inc.halfExtents[f]);
    const auto u = inc.axes[(f + 1) % 3] * inc.halfExtents[(f + 1) % 3];
    const auto v = inc.axes[(f + 2) % 3] * inc.halfExtents[(f + 2) % 3];
    std::vector<V> polygon{c - u - v, c + u - v, c + u + v, c - u + v};
    for (std::size_t i = 0; i < 3; ++i) if (i != face) {
      polygon = clip(polygon, center, ref.axes[i], ref.halfExtents[i]);
      polygon = clip(polygon, center, -ref.axes[i], ref.halfExtents[i]);
    }
    for (const auto p : polygon) {
      const double separation = collisionDot(p - center, n);
      if (separation <= margin) {
        const auto q = p - n * separation;
        result.points.push_back(reverse ? point(p, q, normal) : point(q, p, normal));
      }
    }
    // Keep four well-spread witnesses, rather than the first four clipped vertices.
    if (result.points.size() > 4) {
      auto points = std::move(result.points);
      result.points.push_back(points.front());
      points.erase(points.begin());
      while (result.points.size() < 4) {
        std::size_t selected = 0; double farthest = -1;
        for (std::size_t i = 0; i < points.size(); ++i) {
          double nearest = std::numeric_limits<double>::infinity();
          const auto p = points[i].pointA;
          for (const auto& kept : result.points) {
            const auto d = V{p.x - kept.pointA.x, p.y - kept.pointA.y, p.z - kept.pointA.z};
            nearest = std::min(nearest, collisionDot(d, d));
          }
          if (nearest > farthest) { farthest = nearest; selected = i; }
        }
        result.points.push_back(points[selected]); points.erase(points.begin() + selected);
      }
    }
  } else {
    const std::size_t i = (feature - 6) / 3, j = (feature - 6) % 3;
    auto pa = a.center, pb = b.center;
    for (std::size_t k = 0; k < 3; ++k) {
      if (k != i) pa = pa + a.axes[k] * (collisionDot(a.axes[k], normal) >= 0 ? a.halfExtents[k] : -a.halfExtents[k]);
      if (k != j) pb = pb + b.axes[k] * (collisionDot(b.axes[k], normal) <= 0 ? b.halfExtents[k] : -b.halfExtents[k]);
    }
    const auto d = pa - pb;
    const double c = collisionDot(a.axes[i], b.axes[j]), x = collisionDot(a.axes[i], d), y = collisionDot(b.axes[j], d);
    const double denominator = 1 - c * c;
    double s = denominator > 1e-12 ? (c * y - x) / denominator : 0;
    s = std::clamp(s, -a.halfExtents[i], a.halfExtents[i]);
    const double t = std::clamp(y + c * s, -b.halfExtents[j], b.halfExtents[j]);
    s = std::clamp(c * t - x, -a.halfExtents[i], a.halfExtents[i]);
    result.points.push_back(point(pa + a.axes[i] * s, pb + b.axes[j] * t, normal));
  }
  if (result.points.empty()) result.points.push_back(point(support(a, normal), support(b, -normal), normal));
  return result;
}
} // namespace

void updateBounds(WorldCollider& c) {
  V extent;
  if (c.kind == CollisionKind::Sphere) extent = {c.radius, c.radius, c.radius};
  else for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j) extent[i] += std::abs(c.axes[j][i]) * c.halfExtents[j];
  c.bounds = {c.center - extent, c.center + extent};
}
std::optional<Physics::Manifold> collideManifold(const WorldCollider& a, const WorldCollider& b, double margin) {
  if (!a.bounds.overlaps(b.bounds, margin)) return std::nullopt;
  if (a.kind == CollisionKind::Box && b.kind == CollisionKind::Box) return boxes(a, b, margin);
  if (a.kind == CollisionKind::Sphere && b.kind == CollisionKind::Sphere) {
    const auto delta = b.center - a.center;
    const double distance = collisionLength(delta);
    if (distance - a.radius - b.radius > margin) return std::nullopt;
    const auto n = distance > 1e-12 ? delta * (1 / distance) : V{1, 0, 0};
    return Physics::Manifold{a.entity, b.entity, n.vector(), {point(a.center + n * a.radius, b.center - n * b.radius, n)}};
  }
  const bool reverse = a.kind == CollisionKind::Box;
  const auto& sphere = reverse ? b : a;
  const auto& box = reverse ? a : b;
  const auto delta = sphere.center - box.center;
  V closest = box.center, local;
  bool inside = true;
  for (std::size_t i = 0; i < 3; ++i) {
    local[i] = collisionDot(delta, box.axes[i]);
    inside = inside && std::abs(local[i]) <= box.halfExtents[i];
    closest = closest + box.axes[i] * std::clamp(local[i], -box.halfExtents[i], box.halfExtents[i]);
  }
  V n;
  if (inside) {
    std::size_t face = 0;
    for (std::size_t i = 1; i < 3; ++i)
      if (box.halfExtents[i] - std::abs(local[i]) < box.halfExtents[face] - std::abs(local[face])) face = i;
    const auto outward = box.axes[face] * (local[face] >= 0 ? 1 : -1);
    closest = sphere.center + outward * (box.halfExtents[face] - std::abs(local[face]));
    n = -outward;
  } else {
    V toward;
    for (std::size_t i = 0; i < 3; ++i)
      toward = toward + box.axes[i] * (std::clamp(local[i], -box.halfExtents[i], box.halfExtents[i]) - local[i]);
    const double distance = collisionLength(toward);
    if (distance - sphere.radius > margin) return std::nullopt;
    n = toward * (1 / distance);
  }
  const auto surface = sphere.center + n * sphere.radius;
  if (reverse) return Physics::Manifold{a.entity, b.entity, (-n).vector(), {point(closest, surface, -n)}};
  return Physics::Manifold{a.entity, b.entity, n.vector(), {point(surface, closest, n)}};
}
} // namespace Jade::Detail

namespace Jade::Physics {
std::vector<Manifold> manifolds(const Scene& scene, float distance) {
  if (!std::isfinite(distance) || distance < 0) throw std::invalid_argument("Contact distance must be finite and nonnegative.");
  const auto shapes = Detail::captureColliders(scene);
  std::vector<Manifold> result;
  for (std::size_t i = 0; i < shapes.size(); ++i)
    for (std::size_t j = i + 1; j < shapes.size(); ++j)
      if (auto hit = Detail::collideManifold(shapes[i], shapes[j], distance)) result.push_back(std::move(*hit));
  return result;
}
} // namespace Jade::Physics
