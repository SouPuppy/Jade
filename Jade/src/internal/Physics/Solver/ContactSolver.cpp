#include "ContactSolver.hpp"
#include "../Manifold.hpp"
#include <map>

namespace Jade::Physics::Internal {
V relative(const Body &a, const Body &b, V ra, V rb) {
  return b.velocity + collisionCross(b.angular, rb) - a.velocity - collisionCross(a.angular, ra);
}
double effective(const Body &a, const Body &b, V ra, V rb, V axis) {
  return a.inverseMass + b.inverseMass +
         collisionDot(axis, collisionCross(a.inertia(collisionCross(ra, axis)), ra) +
                                collisionCross(b.inertia(collisionCross(rb, axis)), rb));
}
void impulse(Body &a, Body &b, V ra, V rb, V p) {
  if (a.inverseMass > 0) {
    a.velocity = a.velocity - p * a.inverseMass;
    a.angular = a.angular - a.inertia(collisionCross(ra, p));
  }
  if (b.inverseMass > 0) {
    b.velocity = b.velocity + p * b.inverseMass;
    b.angular = b.angular + b.inertia(collisionCross(rb, p));
  }
}
// Refresh paired surface anchors instead of replacing them with an arbitrarily
// far-away intersection of two almost parallel clipping planes each frame.
bool validAnchor(const Body &a, const Body &b, V localA, V localB, V normal, double margin) {
  const auto delta = b.shape.center + b.offset(localB) - a.shape.center - a.offset(localA);
  const double separation = collisionDot(delta, normal);
  const auto tangent = delta - normal * separation;
  const double tolerance =
      margin + 8 * std::numeric_limits<float>::epsilon() * std::max(a.shape.size(), b.shape.size());
  return separation <= tolerance && collisionDot(tangent, tangent) <= tolerance * tolerance;
}
std::vector<ContactPoint> persistentPoints(const Body &a, const Body &b, const Manifold &hit,
                                           std::span<const Cached> cache, double margin) {
  if (a.shape.kind != Detail::CollisionKind::Box || b.shape.kind != Detail::CollisionKind::Box)
    return hit.points;
  const auto normal = v(hit.normal);
  std::vector<ContactPoint> points;
  for (const auto &old : cache) {
    if (a.geometryEdited || b.geometryEdited || old.a != hit.a || old.b != hit.b ||
        collisionDot(old.normal, normal) < 0.99 || !validAnchor(a, b, old.localA, old.localB, normal, margin))
      continue;
    const auto pa = a.shape.center + a.offset(old.localA), pb = b.shape.center + b.offset(old.localB);
    points.push_back({pa.vector(), pb.vector(), static_cast<float>(collisionDot(pb - pa, normal))});
  }
  const double weld =
      std::max(8 * std::numeric_limits<float>::epsilon() * std::min(a.shape.size(), b.shape.size()),
               std::min(margin, 0.01 * std::min(a.shape.size(), b.shape.size())));
  const auto append = [&](const ContactPoint &next) {
    const auto duplicate = std::any_of(points.begin(), points.end(), [&](const ContactPoint &kept) {
      const auto da = v(next.pointA) - v(kept.pointA), db = v(next.pointB) - v(kept.pointB);
      return collisionDot(da, da) + collisionDot(db, db) < weld * weld * 2;
    });
    if (!duplicate)
      points.push_back(next);
  };
  if (points.empty() && margin > 0) {
    // Cold starts need the same finite contact patch as retained anchors. For
    // nearly parallel faces, closest corner/face witnesses within the solver's
    // margin avoid dividing micron offsets by a nearly zero edge-plane angle.
    std::size_t fa = 0, fb = 0;
    for (std::size_t i = 1; i < 3; ++i) {
      if (std::abs(collisionDot(a.shape.axes[i], normal)) > std::abs(collisionDot(a.shape.axes[fa], normal)))
        fa = i;
      if (std::abs(collisionDot(b.shape.axes[i], normal)) > std::abs(collisionDot(b.shape.axes[fb], normal)))
        fb = i;
    }
    const double tilt = std::max(collisionLength(collisionCross(a.shape.axes[fa], normal)),
                                 collisionLength(collisionCross(b.shape.axes[fb], normal)));
    if (tilt * std::max(a.shape.size(), b.shape.size()) <= margin * 0.25) {
      const auto corners = [&](const Body &source, std::size_t face, const Body &target,
                               std::size_t targetFace, bool reverse) {
        const auto direction = reverse ? -normal : normal;
        const auto center = source.shape.center +
                            source.shape.axes[face] * (collisionDot(source.shape.axes[face], direction) >= 0
                                                           ? source.shape.halfExtents[face]
                                                           : -source.shape.halfExtents[face]);
        const auto u = source.shape.axes[(face + 1) % 3] * source.shape.halfExtents[(face + 1) % 3];
        const auto w = source.shape.axes[(face + 2) % 3] * source.shape.halfExtents[(face + 2) % 3];
        for (const auto p : {center - u - w, center + u - w, center + u + w, center - u + w}) {
          auto q = target.shape.center;
          for (std::size_t i = 0; i < 3; ++i) {
            const double coordinate =
                i == targetFace
                    ? (collisionDot(target.shape.axes[i], direction) > 0 ? -target.shape.halfExtents[i]
                                                                         : target.shape.halfExtents[i])
                    : std::clamp(collisionDot(p - target.shape.center, target.shape.axes[i]),
                                 -target.shape.halfExtents[i], target.shape.halfExtents[i]);
            q = q + target.shape.axes[i] * coordinate;
          }
          const auto pa = reverse ? q : p, pb = reverse ? p : q;
          if (validAnchor(a, b, a.local(pa), b.local(pb), normal, margin))
            append({pa.vector(), pb.vector(), static_cast<float>(collisionDot(pb - pa, normal))});
        }
      };
      corners(a, fa, b, fb, false);
      corners(b, fb, a, fa, true);
    }
  }
  for (const auto &next : hit.points)
    append(next);
  if (points.size() <= 4)
    return points;
  // Keep the deepest witness and maximize the covered contact area. Cached
  // anchors come first so equally good patches retain their original points.
  const auto deepest =
      static_cast<std::size_t>(std::min_element(points.begin(), points.end(),
                                                [](const ContactPoint &x, const ContactPoint &y) {
                                                  return x.separation < y.separation;
                                                }) -
                               points.begin());
  std::array<std::size_t, 4> selected{};
  double best = -1;
  for (std::size_t i = 0; i < points.size(); ++i)
    for (std::size_t j = i + 1; j < points.size(); ++j)
      for (std::size_t k = j + 1; k < points.size(); ++k)
        for (std::size_t l = k + 1; l < points.size(); ++l) {
          if (i != deepest && j != deepest && k != deepest && l != deepest)
            continue;
          const std::array<V, 4> p{v(points[i].pointA), v(points[j].pointA), v(points[k].pointA),
                                   v(points[l].pointA)};
          double area = 0;
          for (std::size_t diagonal = 1; diagonal < 4; ++diagonal) {
            const auto u = p[diagonal] - p[0];
            const auto w = p[diagonal % 3 + 1] - p[(diagonal + 1) % 3 + 1];
            area = std::max(area, std::abs(collisionDot(normal, collisionCross(u, w))));
          }
          if (area > best + 1e-10 * a.shape.size() * b.shape.size()) {
            best = area;
            selected = {i, j, k, l};
          }
        }
  std::vector<ContactPoint> reduced;
  for (const auto i : selected)
    reduced.push_back(points[i]);
  return reduced;
}
void positionImpulse(Body &a, Body &b, V ra, V rb, V p) {
  if (a.inverseMass > 0)
    a.shape.center = a.shape.center - p * a.inverseMass;
  if (b.inverseMass > 0)
    b.shape.center = b.shape.center + p * b.inverseMass;
  auto aa = -a.inertia(collisionCross(ra, p)), ab = b.inertia(collisionCross(rb, p));
  const auto limit = [](V angle) {
    const double length = collisionLength(angle);
    return length > 0.2 ? angle * (0.2 / length) : angle;
  };
  if (a.inverseMass > 0)
    a.turn(limit(aa));
  if (b.inverseMass > 0)
    b.turn(limit(ab));
}
void solveIsland(std::span<Body> bodies, std::span<Constraint> constraints,
                 std::span<const std::size_t> island, const Config &config) {
  for (const auto ci : island) {
    const auto &c = constraints[ci];
    auto &a = bodies[c.a];
    auto &b = bodies[c.b];
    impulse(a, b, a.offset(c.localA), b.offset(c.localB),
            c.normal * c.normalImpulse + c.tangent1 * c.tangentImpulse1 + c.tangent2 * c.tangentImpulse2);
  }
  for (unsigned iteration = 0; iteration < config.velocityIterations; ++iteration) {
    for (const auto ci : island) {
      auto &c = constraints[ci];
      auto &a = bodies[c.a];
      auto &b = bodies[c.b];
      const auto ra = a.offset(c.localA), rb = b.offset(c.localB);
      const double mass = effective(a, b, ra, rb, c.normal);
      if (mass <= 0)
        continue;
      const double old = c.normalImpulse;
      c.normalImpulse =
          std::max(0.0, old + (c.target - collisionDot(relative(a, b, ra, rb), c.normal)) / mass);
      impulse(a, b, ra, rb, c.normal * (c.normalImpulse - old));
      const auto rv = relative(a, b, ra, rb);
      const double m1 = effective(a, b, ra, rb, c.tangent1), m2 = effective(a, b, ra, rb, c.tangent2);
      double next1 = c.tangentImpulse1 - collisionDot(rv, c.tangent1) / m1;
      double next2 = c.tangentImpulse2 - collisionDot(rv, c.tangent2) / m2;
      const double limit = c.friction * c.normalImpulse, length = std::hypot(next1, next2);
      if (length > limit && length > 0) {
        next1 *= limit / length;
        next2 *= limit / length;
      }
      impulse(a, b, ra, rb,
              c.tangent1 * (next1 - c.tangentImpulse1) + c.tangent2 * (next2 - c.tangentImpulse2));
      c.tangentImpulse1 = next1;
      c.tangentImpulse2 = next2;
    }
  }
  for (unsigned iteration = 0; iteration < config.positionIterations; ++iteration) {
    for (const auto ci : island) {
      const auto &c = constraints[ci];
      auto &a = bodies[c.a];
      auto &b = bodies[c.b];
      const auto ra = a.offset(c.localA), rb = b.offset(c.localB);
      const double separation = collisionDot(b.shape.center + rb - a.shape.center - ra, c.normal);
      const double error = std::min(0.0, separation + config.penetrationSlop);
      const double mass = effective(a, b, ra, rb, c.normal);
      if (mass > 0 && error < 0)
        positionImpulse(a, b, ra, rb, c.normal * (-0.4 * error / mass));
    }
  }
}
std::vector<Constraint> buildConstraints(const Body &a, const Body &b, std::size_t i, std::size_t j,
                                         std::span<const Cached> previousContacts, const Config &config,
                                         float dt, double previousDt) {
  std::vector<Constraint> result;
  const auto hit = Detail::collideManifold(a.shape, b.shape, config.contactDistance);
  if (!hit)
    return result;
  const auto normal = v(hit->normal);
  const auto seed = std::abs(normal.x) < 0.7 ? V{1, 0, 0} : V{0, 1, 0};
  const auto t1 = Detail::collisionUnit(collisionCross(normal, seed));
  const auto t2 = collisionCross(normal, t1);
  std::vector<bool> used(previousContacts.size());
  for (const auto &p : persistentPoints(a, b, *hit, previousContacts, config.contactDistance)) {
    Constraint c{i, j, normal, t1, t2, a.local(v(p.pointA)), b.local(v(p.pointB))};
    c.friction = std::sqrt(static_cast<double>(a.properties.friction) * b.properties.friction);
    const auto ra = v(p.pointA) - a.shape.center, rb = v(p.pointB) - b.shape.center;
    const double speed = collisionDot(relative(a, b, ra, rb), normal);
    const double restitution = std::max(a.properties.restitution, b.properties.restitution);
    // A fast impact can enter the retention margin before crossing the exact
    // surface. Preserve its rebound instead of erasing it with speculation.
    c.target =
        speed < -1 && restitution > 0 ? -restitution * speed : (p.separation > 0 ? -p.separation / dt : 0);
    if (!a.edited && !b.edited && previousDt > 0) {
      std::size_t best = previousContacts.size();
      double distance = 0.02 * 0.02 * 2;
      for (std::size_t k = 0; k < previousContacts.size(); ++k) {
        const auto &old = previousContacts[k];
        if (used[k] || old.a != a.shape.entity || old.b != b.shape.entity ||
            collisionDot(normal, old.normal) < 0.99)
          continue;
        const auto da = old.localA - c.localA, db = old.localB - c.localB;
        const double next = collisionDot(da, da) + collisionDot(db, db);
        if (next < distance) {
          best = k;
          distance = next;
        }
      }
      if (best < previousContacts.size()) {
        used[best] = true;
        const auto &old = previousContacts[best];
        const double scale = std::min(2.0, dt / previousDt);
        c.normalImpulse = old.impulse * scale;
        c.tangentImpulse1 = collisionDot(old.tangent, t1) * scale;
        c.tangentImpulse2 = collisionDot(old.tangent, t2) * scale;
      }
    }
    result.push_back(c);
  }
  return result;
}
std::vector<std::vector<std::size_t>> contactIslands(std::span<const Constraint> constraints,
                                                     std::span<const unsigned char> dynamic) {
  std::vector<std::vector<std::size_t>> islands;
  std::vector<std::size_t> parents(dynamic.size());
  for (std::size_t i = 0; i < parents.size(); ++i)
    parents[i] = i;
  const auto root = [&](std::size_t index) {
    while (parents[index] != index) {
      parents[index] = parents[parents[index]];
      index = parents[index];
    }
    return index;
  };
  for (const auto &c : constraints)
    if (dynamic[c.a] && dynamic[c.b]) {
      const auto a = root(c.a), b = root(c.b);
      parents[std::max(a, b)] = std::min(a, b);
    }
  std::map<std::size_t, std::size_t> indices;
  for (std::size_t i = 0; i < constraints.size(); ++i) {
    const auto &c = constraints[i];
    const auto owner = root(dynamic[c.a] ? c.a : c.b);
    const auto [it, inserted] = indices.emplace(owner, islands.size());
    if (inserted)
      islands.emplace_back();
    islands[it->second].push_back(i);
  }
  return islands;
}
} // namespace Jade::Physics::Internal
