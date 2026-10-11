#include <Jade/Physics/System.hpp>
#include <Jade/Core/Transform.hpp>
#include "../internal/Physics/Manifold.hpp"
#include <algorithm>
#include <map>
#include <numbers>

namespace Jade::Physics {
namespace {
using V = Detail::CollisionVector;
using Detail::collisionDot;
using Detail::collisionCross;
using Detail::collisionLength;
V v(Vec3 p) { return {p.x, p.y, p.z}; }
void finite(Vec3 p) {
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
    throw std::invalid_argument("RigidBody vectors must be finite.");
}
void validate(const RigidBody& b) {
  finite(b.velocity); finite(b.angularVelocity);
  if (!std::isfinite(b.mass) || b.mass < 0 || !std::isfinite(b.friction) || b.friction < 0 ||
      !std::isfinite(b.restitution) || b.restitution < 0 || b.restitution > 1)
    throw std::invalid_argument("RigidBody mass/friction must be nonnegative; restitution must be in [0,1].");
}
Quaternion rotation(V angle) {
  const double length = collisionLength(angle);
  if (length < 1e-12) return {};
  const double s = std::sin(length * 0.5) / length;
  return Quaternion{static_cast<float>(std::cos(length * 0.5)), static_cast<float>(angle.x * s),
                    static_cast<float>(angle.y * s), static_cast<float>(angle.z * s)}.normalized();
}
struct Body {
  Detail::WorldCollider shape;
  Transform pose;
  RigidBody properties;
  V velocity, angular, inverseInertia;
  double inverseMass = 0;
  bool edited = true, geometryEdited = true;
  V inertia(V value) const {
    V result;
    for (std::size_t i = 0; i < 3; ++i)
      result = result + shape.axes[i] * (collisionDot(shape.axes[i], value) * inverseInertia[i]);
    return result;
  }
  V local(V p) const {
    V result;
    for (std::size_t i = 0; i < 3; ++i) result[i] = collisionDot(p - shape.center, shape.axes[i]);
    return result;
  }
  V offset(V p) const {
    V result;
    for (std::size_t i = 0; i < 3; ++i) result = result + shape.axes[i] * p[i];
    return result;
  }
  void turn(V angle) {
    if (collisionLength(angle) < 1e-12) return;
    const auto q = rotation(angle);
    pose.rotation = (q * pose.rotation).normalized();
    for (auto& axis : shape.axes) axis = v(q.rotate(axis.vector()));
  }
};
Body body(const Scene& scene, const Detail::WorldCollider& shape) {
  Body b;
  b.shape = shape;
  b.pose = shape.entity.get<Transform>();
  if (!shape.entity.has<RigidBody>()) { b.properties.mass = 0; return b; }
  b.properties = shape.entity.get<RigidBody>(); validate(b.properties);
  if (b.properties.mass == 0) return b;
  if (scene.parent(shape.entity)) throw std::invalid_argument("Dynamic RigidBody must be a Scene root.");
  b.pose.rotation = b.pose.rotation.normalized();
  b.inverseMass = 1.0 / b.properties.mass;
  b.velocity = v(b.properties.velocity); b.angular = v(b.properties.angularVelocity);
  if (shape.kind == Detail::CollisionKind::Sphere) {
    const double inverse = 2.5 * b.inverseMass / (shape.radius * shape.radius);
    b.inverseInertia = {inverse, inverse, inverse};
  } else for (std::size_t i = 0; i < 3; ++i) {
    const auto half = shape.halfExtents;
    const double a = half[(i + 1) % 3], c = half[(i + 2) % 3];
    b.inverseInertia[i] = 3 * b.inverseMass / (a * a + c * c);
  }
  return b;
}
V relative(const Body& a, const Body& b, V ra, V rb) {
  return b.velocity + collisionCross(b.angular, rb) - a.velocity - collisionCross(a.angular, ra);
}
double effective(const Body& a, const Body& b, V ra, V rb, V axis) {
  return a.inverseMass + b.inverseMass +
    collisionDot(axis, collisionCross(a.inertia(collisionCross(ra, axis)), ra) +
                       collisionCross(b.inertia(collisionCross(rb, axis)), rb));
}
void impulse(Body& a, Body& b, V ra, V rb, V p) {
  a.velocity = a.velocity - p * a.inverseMass;
  b.velocity = b.velocity + p * b.inverseMass;
  a.angular = a.angular - a.inertia(collisionCross(ra, p));
  b.angular = b.angular + b.inertia(collisionCross(rb, p));
}
struct Constraint {
  std::size_t a, b;
  V normal, tangent1, tangent2, localA, localB;
  double normalImpulse = 0, tangentImpulse1 = 0, tangentImpulse2 = 0;
  double target = 0, friction = 0;
};
struct Cached {
  Entity a, b;
  V normal, localA, localB, tangent;
  double impulse = 0;
};
bool near(V a, V b, double tolerance = 1e-5) { return collisionLength(a - b) < tolerance; }
bool sameGeometry(const Body& a, const Body& b) {
  if (a.shape.entity != b.shape.entity || a.shape.kind != b.shape.kind ||
      a.shape.radius != b.shape.radius || !near(a.shape.halfExtents, b.shape.halfExtents) ||
      !near(a.shape.center, b.shape.center)) return false;
  for (std::size_t i = 0; i < 3; ++i) if (!near(a.shape.axes[i], b.shape.axes[i])) return false;
  return true;
}
bool same(const Body& a, const Body& b) {
  return sameGeometry(a, b) && a.properties.mass == b.properties.mass &&
    a.properties.friction == b.properties.friction && a.properties.restitution == b.properties.restitution &&
    a.properties.velocity == b.properties.velocity && a.properties.angularVelocity == b.properties.angularVelocity;
}
// Refresh paired surface anchors instead of replacing them with an arbitrarily
// far-away intersection of two almost parallel clipping planes each frame.
bool validAnchor(const Body& a, const Body& b, V localA, V localB, V normal, double margin) {
  const auto delta = b.shape.center + b.offset(localB) - a.shape.center - a.offset(localA);
  const double separation = collisionDot(delta, normal);
  const auto tangent = delta - normal * separation;
  const double tolerance = margin + 8 * std::numeric_limits<float>::epsilon() *
    std::max(a.shape.size(), b.shape.size());
  return separation <= tolerance && collisionDot(tangent, tangent) <= tolerance * tolerance;
}
std::vector<ContactPoint> persistentPoints(const Body& a, const Body& b, const Manifold& hit,
                                          const std::vector<Cached>& cache, double margin) {
  if (a.geometryEdited || b.geometryEdited || a.shape.kind != Detail::CollisionKind::Box ||
      b.shape.kind != Detail::CollisionKind::Box) return hit.points;
  const auto normal = v(hit.normal);
  std::vector<ContactPoint> points;
  for (const auto& old : cache) {
    if (old.a != hit.a || old.b != hit.b || collisionDot(old.normal, normal) < 0.99 ||
        !validAnchor(a, b, old.localA, old.localB, normal, margin)) continue;
    const auto pa = a.shape.center + a.offset(old.localA), pb = b.shape.center + b.offset(old.localB);
    points.push_back({pa.vector(), pb.vector(), static_cast<float>(collisionDot(pb - pa, normal))});
  }
  for (const auto& next : hit.points) {
    const auto duplicate = std::any_of(points.begin(), points.end(), [&](const ContactPoint& kept) {
      const auto da = v(next.pointA) - v(kept.pointA), db = v(next.pointB) - v(kept.pointB);
      return collisionDot(da, da) + collisionDot(db, db) < margin * margin * 2;
    });
    if (!duplicate) points.push_back(next);
  }
  if (points.size() <= 4) return points;
  // Keep the deepest witness and maximize the covered contact area. Cached
  // anchors come first so equally good patches retain their original points.
  const auto deepest = static_cast<std::size_t>(std::min_element(points.begin(), points.end(),
    [](const ContactPoint& x, const ContactPoint& y) { return x.separation < y.separation; }) - points.begin());
  std::array<std::size_t, 4> selected{};
  double best = -1;
  for (std::size_t i = 0; i < points.size(); ++i)
    for (std::size_t j = i + 1; j < points.size(); ++j)
      for (std::size_t k = j + 1; k < points.size(); ++k)
        for (std::size_t l = k + 1; l < points.size(); ++l) {
          if (i != deepest && j != deepest && k != deepest && l != deepest) continue;
          const std::array<V, 4> p{v(points[i].pointA), v(points[j].pointA), v(points[k].pointA), v(points[l].pointA)};
          double area = 0;
          for (std::size_t diagonal = 1; diagonal < 4; ++diagonal) {
            const auto u = p[diagonal] - p[0];
            const auto w = p[diagonal % 3 + 1] - p[(diagonal + 1) % 3 + 1];
            area = std::max(area, std::abs(collisionDot(normal, collisionCross(u, w))));
          }
          if (area > best + 1e-10 * a.shape.size() * b.shape.size()) {
            best = area; selected = {i, j, k, l};
          }
        }
  std::vector<ContactPoint> reduced;
  for (const auto i : selected) reduced.push_back(points[i]);
  return reduced;
}
void positionImpulse(Body& a, Body& b, V ra, V rb, V p) {
  a.shape.center = a.shape.center - p * a.inverseMass;
  b.shape.center = b.shape.center + p * b.inverseMass;
  auto aa = -a.inertia(collisionCross(ra, p)), ab = b.inertia(collisionCross(rb, p));
  const auto limit = [](V angle) {
    const double length = collisionLength(angle);
    return length > 0.2 ? angle * (0.2 / length) : angle;
  };
  if (a.inverseMass > 0) a.turn(limit(aa));
  if (b.inverseMass > 0) b.turn(limit(ab));
}
} // namespace

struct System::Impl {
  Config config;
  Statistics stats;
  std::vector<Manifold> contacts;
  std::vector<Cached> cache;
  std::vector<Body> previous;
  double previousDt = 0;
  explicit Impl(Config c) : config(c) {}
};
System::System(Config config) : m_Impl(std::make_unique<Impl>(config)) {
  finite(config.gravity);
  if (!std::isfinite(config.fixedStep) || config.fixedStep <= 0 || config.maxSubsteps == 0 ||
      config.velocityIterations == 0 || config.positionIterations == 0 ||
      !std::isfinite(config.contactDistance) || !std::isfinite(config.penetrationSlop) ||
      config.penetrationSlop < 0 || config.contactDistance < config.penetrationSlop)
    throw std::invalid_argument("Invalid Physics configuration.");
}
System::~System() = default;
const Config& System::config() const noexcept { return m_Impl->config; }
const Statistics& System::statistics() const noexcept { return m_Impl->stats; }
std::span<const Manifold> System::contacts() const noexcept { return m_Impl->contacts; }
std::vector<Manifold> System::contactSnapshot(const Scene& scene) const {
  const auto shapes = Detail::captureColliders(scene);
  bool current = m_Impl->previousDt > 0 && shapes.size() == m_Impl->previous.size();
  for (std::size_t i = 0; current && i < shapes.size(); ++i) {
    const auto& pose = shapes[i].entity.get<Transform>(); const auto& old = m_Impl->previous[i];
    current = pose.position == old.pose.position && pose.rotation == old.pose.rotation &&
      pose.scale == old.pose.scale && same(body(scene, shapes[i]), old);
  }
  if (!current) return manifolds(scene);
  auto result = m_Impl->contacts;
  // The solver skips static/static pairs; the debug view still shows them.
  for (std::size_t i = 0; i < shapes.size(); ++i)
    for (std::size_t j = i + 1; j < shapes.size(); ++j)
      if (!m_Impl->previous[i].inverseMass && !m_Impl->previous[j].inverseMass)
        if (auto hit = Detail::collideManifold(shapes[i], shapes[j], 0)) result.push_back(std::move(*hit));
  return result;
}
void System::reset() noexcept {
  m_Impl->cache.clear(); m_Impl->contacts.clear(); m_Impl->previous.clear();
  m_Impl->previousDt = 0; m_Impl->stats = {};
}
void System::applyImpulse(Scene& scene, Entity entity, Vec3 p, Vec3 worldPoint) {
  finite(p); finite(worldPoint);
  if (!scene.owns(entity) || !entity.has<RigidBody>() || !entity.has<Collider>() || !entity.has<Transform>())
    throw std::invalid_argument("Impulse requires a live Transform/Collider/RigidBody entity.");
  auto b = body(scene, Detail::resolveCollider(entity, scene.worldMatrix(entity)));
  if (!b.inverseMass) return;
  const auto nextVelocity = (b.velocity + v(p) * b.inverseMass).vector();
  const auto nextAngular = (b.angular + b.inertia(collisionCross(v(worldPoint) - b.shape.center, v(p)))).vector();
  auto& rb = entity.get<RigidBody>(); rb.velocity = nextVelocity; rb.angularVelocity = nextAngular;
  for (auto& contact : m_Impl->cache) { contact.impulse = 0; contact.tangent = {}; }
}
void System::step(Scene& scene, float dt) {
  if (!std::isfinite(dt) || dt <= 0) throw std::invalid_argument("Physics step must be positive and finite.");
  const auto& config = m_Impl->config;
  for (const auto entity : scene.query<RigidBody>()) {
    const auto& rb = entity.get<RigidBody>();
    validate(rb);
    if (rb.mass > 0 && (!entity.has<Transform>() || !entity.has<Collider>()))
      throw std::invalid_argument("Dynamic RigidBody requires Transform and Collider.");
  }
  auto proxies = Detail::captureColliders(scene);
  std::vector<Body> bodies;
  bodies.reserve(proxies.size());
  Statistics stats; stats.steps = m_Impl->stats.steps + 1;
  // Validate the complete input before integrating or mutating any component.
  for (const auto& shape : proxies) {
    auto b = body(scene, shape);
    const auto old = std::find_if(m_Impl->previous.begin(), m_Impl->previous.end(),
      [&](const Body& previous) { return previous.shape.entity == shape.entity; });
    b.edited = old == m_Impl->previous.end() || !same(b, *old);
    b.geometryEdited = old == m_Impl->previous.end() || !sameGeometry(b, *old);
    if (b.inverseMass > 0) ++stats.dynamicBodies; else ++stats.staticBodies;
    bodies.push_back(b);
  }
  for (auto& b : bodies) if (b.inverseMass > 0) {
    b.velocity = b.velocity + v(config.gravity) * dt;
    b.shape.center = b.shape.center + b.velocity * dt;
    b.turn(b.angular * dt);
    Detail::updateBounds(b.shape);
  }
  std::vector<Constraint> constraints;
  std::vector<Manifold> contacts;
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    for (std::size_t j = i + 1; j < bodies.size(); ++j) {
      auto& a = bodies[i]; auto& b = bodies[j];
      if (!a.inverseMass && !b.inverseMass) continue;
      const auto hit = Detail::collideManifold(a.shape, b.shape, config.contactDistance);
      if (!hit) continue;
      const auto normal = v(hit->normal);
      const auto seed = std::abs(normal.x) < 0.7 ? V{1, 0, 0} : V{0, 1, 0};
      const auto t1 = Detail::collisionUnit(collisionCross(normal, seed));
      const auto t2 = collisionCross(normal, t1);
      std::vector<bool> used(m_Impl->cache.size());
      for (const auto& p : persistentPoints(a, b, *hit, m_Impl->cache, config.contactDistance)) {
        Constraint c{i, j, normal, t1, t2, a.local(v(p.pointA)), b.local(v(p.pointB))};
        c.friction = std::sqrt(static_cast<double>(a.properties.friction) * b.properties.friction);
        const auto ra = v(p.pointA) - a.shape.center, rb = v(p.pointB) - b.shape.center;
        const double speed = collisionDot(relative(a, b, ra, rb), normal);
        const double restitution = std::max(a.properties.restitution, b.properties.restitution);
        // A fast impact can enter the retention margin before crossing the exact
        // surface. Preserve its rebound instead of erasing it with speculation.
        c.target = speed < -1 && restitution > 0 ? -restitution * speed :
          (p.separation > 0 ? -p.separation / dt : 0);
        if (!a.edited && !b.edited && m_Impl->previousDt > 0) {
          std::size_t best = m_Impl->cache.size(); double distance = 0.02 * 0.02 * 2;
          for (std::size_t k = 0; k < m_Impl->cache.size(); ++k) {
            const auto& old = m_Impl->cache[k];
            if (used[k] || old.a != a.shape.entity || old.b != b.shape.entity || collisionDot(normal, old.normal) < 0.99) continue;
            const auto da = old.localA - c.localA, db = old.localB - c.localB;
            const double next = collisionDot(da, da) + collisionDot(db, db);
            if (next < distance) { best = k; distance = next; }
          }
          if (best < m_Impl->cache.size()) {
            used[best] = true;
            const auto& old = m_Impl->cache[best];
            const double scale = std::min(2.0, dt / m_Impl->previousDt);
            c.normalImpulse = old.impulse * scale;
            c.tangentImpulse1 = collisionDot(old.tangent, t1) * scale;
            c.tangentImpulse2 = collisionDot(old.tangent, t2) * scale;
          }
        }
        constraints.push_back(c);
      }
    }
  }
  for (const auto& c : constraints) {
    auto& a = bodies[c.a]; auto& b = bodies[c.b];
    impulse(a, b, a.offset(c.localA), b.offset(c.localB),
      c.normal * c.normalImpulse + c.tangent1 * c.tangentImpulse1 + c.tangent2 * c.tangentImpulse2);
  }
  for (unsigned iteration = 0; iteration < config.velocityIterations; ++iteration) {
    for (auto& c : constraints) {
      auto& a = bodies[c.a]; auto& b = bodies[c.b];
      const auto ra = a.offset(c.localA), rb = b.offset(c.localB);
      const double mass = effective(a, b, ra, rb, c.normal);
      if (mass <= 0) continue;
      const double old = c.normalImpulse;
      c.normalImpulse = std::max(0.0, old + (c.target - collisionDot(relative(a, b, ra, rb), c.normal)) / mass);
      impulse(a, b, ra, rb, c.normal * (c.normalImpulse - old));
      const auto rv = relative(a, b, ra, rb);
      const double m1 = effective(a, b, ra, rb, c.tangent1), m2 = effective(a, b, ra, rb, c.tangent2);
      double next1 = c.tangentImpulse1 - collisionDot(rv, c.tangent1) / m1;
      double next2 = c.tangentImpulse2 - collisionDot(rv, c.tangent2) / m2;
      const double limit = c.friction * c.normalImpulse, length = std::hypot(next1, next2);
      if (length > limit && length > 0) { next1 *= limit / length; next2 *= limit / length; }
      impulse(a, b, ra, rb, c.tangent1 * (next1 - c.tangentImpulse1) + c.tangent2 * (next2 - c.tangentImpulse2));
      c.tangentImpulse1 = next1; c.tangentImpulse2 = next2;
    }
  }
  for (unsigned iteration = 0; iteration < config.positionIterations; ++iteration) {
    for (const auto& c : constraints) {
      auto& a = bodies[c.a]; auto& b = bodies[c.b];
      const auto ra = a.offset(c.localA), rb = b.offset(c.localB);
      const double separation = collisionDot(b.shape.center + rb - a.shape.center - ra, c.normal);
      const double error = std::min(0.0, separation + config.penetrationSlop);
      const double mass = effective(a, b, ra, rb, c.normal);
      if (mass > 0 && error < 0)
        positionImpulse(a, b, ra, rb, c.normal * (-0.4 * error / mass));
    }
  }
  // Prepare every output and cache allocation before committing Scene changes.
  std::vector<Cached> cache;
  cache.reserve(constraints.size());
  for (auto& b : bodies) if (b.inverseMass > 0) {
    b.pose.position = b.shape.center.vector();
    b.properties.velocity = b.velocity.vector(); b.properties.angularVelocity = b.angular.vector();
    Detail::updateBounds(b.shape);
  }
  for (const auto& c : constraints) {
    const auto& a = bodies[c.a]; const auto& b = bodies[c.b];
    if (!validAnchor(a, b, c.localA, c.localB, c.normal, config.contactDistance)) continue;
    const auto pa = a.shape.center + a.offset(c.localA), pb = b.shape.center + b.offset(c.localB);
    const auto friction = c.tangent1 * c.tangentImpulse1 + c.tangent2 * c.tangentImpulse2;
    ContactPoint p{pa.vector(), pb.vector(), V{collisionDot(pb - pa, c.normal), 0, 0}.vector().x,
      V{c.normalImpulse, 0, 0}.vector().x, friction.vector()};
    if (contacts.empty() || contacts.back().a != a.shape.entity || contacts.back().b != b.shape.entity)
      contacts.push_back({a.shape.entity, b.shape.entity, c.normal.vector(), {}});
    contacts.back().points.push_back(p);
    cache.push_back({a.shape.entity, b.shape.entity, c.normal, c.localA, c.localB, friction, c.normalImpulse});
    ++stats.contacts;
    stats.maximumPenetration = std::max(stats.maximumPenetration, -p.separation);
  }
  for (const auto& b : bodies) if (b.inverseMass > 0) {
    b.shape.entity.get<Transform>() = b.pose;
    b.shape.entity.get<RigidBody>() = b.properties;
  }
  m_Impl->cache = std::move(cache); m_Impl->previous = std::move(bodies);
  m_Impl->contacts = std::move(contacts); m_Impl->stats = stats; m_Impl->previousDt = dt;
}
} // namespace Jade::Physics
