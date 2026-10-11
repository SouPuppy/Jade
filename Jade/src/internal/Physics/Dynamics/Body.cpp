#include "Body.hpp"
#include <numbers>

namespace Jade::Physics::Internal {

using Detail::collisionCross;
using Detail::collisionDot;
using Detail::collisionLength;
V v(Vec3 p) {
  return {p.x, p.y, p.z};
}
void finite(Vec3 p) {
  if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
    throw std::invalid_argument("RigidBody vectors must be finite.");
}
void validate(const RigidBody &b) {
  finite(b.velocity);
  finite(b.angularVelocity);
  if (!std::isfinite(b.mass) || b.mass < 0 || !std::isfinite(b.friction) || b.friction < 0 ||
      !std::isfinite(b.restitution) || b.restitution < 0 || b.restitution > 1)
    throw std::invalid_argument("RigidBody mass/friction must be nonnegative; restitution must be in [0,1].");
}
Quaternion rotation(V angle) {
  const double length = collisionLength(angle);
  if (length < 1e-12)
    return {};
  const double s = std::sin(length * 0.5) / length;
  return Quaternion{static_cast<float>(std::cos(length * 0.5)), static_cast<float>(angle.x * s),
                    static_cast<float>(angle.y * s), static_cast<float>(angle.z * s)}
      .normalized();
}
Body body(const Scene &scene, const Detail::WorldCollider &shape) {
  Body b;
  b.shape = shape;
  b.pose = shape.entity.get<Transform>();
  if (!shape.entity.has<RigidBody>()) {
    b.properties.mass = 0;
    return b;
  }
  b.properties = shape.entity.get<RigidBody>();
  validate(b.properties);
  if (b.properties.mass == 0)
    return b;
  if (scene.parent(shape.entity))
    throw std::invalid_argument("Dynamic RigidBody must be a Scene root.");
  b.pose.rotation = b.pose.rotation.normalized();
  b.inverseMass = 1.0 / b.properties.mass;
  b.velocity = v(b.properties.velocity);
  b.angular = v(b.properties.angularVelocity);
  if (shape.kind == Detail::CollisionKind::Sphere) {
    const double inverse = 2.5 * b.inverseMass / (shape.radius * shape.radius);
    b.inverseInertia = {inverse, inverse, inverse};
  } else
    for (std::size_t i = 0; i < 3; ++i) {
      const auto half = shape.halfExtents;
      const double a = half[(i + 1) % 3], c = half[(i + 2) % 3];
      b.inverseInertia[i] = 3 * b.inverseMass / (a * a + c * c);
    }
  return b;
}
bool near(V a, V b, double tolerance = 1e-5) {
  return collisionLength(a - b) < tolerance;
}
bool sameGeometry(const Body &a, const Body &b) {
  if (a.shape.entity != b.shape.entity || a.shape.kind != b.shape.kind || a.shape.radius != b.shape.radius ||
      !near(a.shape.halfExtents, b.shape.halfExtents) || !near(a.shape.center, b.shape.center))
    return false;
  for (std::size_t i = 0; i < 3; ++i)
    if (!near(a.shape.axes[i], b.shape.axes[i]))
      return false;
  return true;
}
bool same(const Body &a, const Body &b) {
  return sameGeometry(a, b) && a.properties.mass == b.properties.mass &&
         a.properties.friction == b.properties.friction &&
         a.properties.restitution == b.properties.restitution &&
         a.properties.velocity == b.properties.velocity &&
         a.properties.angularVelocity == b.properties.angularVelocity;
}
std::vector<FieldBody> fieldBodies(std::span<const Body> bodies) {
  std::vector<FieldBody> snapshot;
  snapshot.reserve(bodies.size());
  for (const auto &b : bodies)
    snapshot.push_back({b.shape.entity, b.shape.center.vector(), b.velocity.vector(), b.properties.mass});
  return snapshot;
}
std::vector<V> sampleForces(const Environment &environment, std::span<const FieldBody> snapshot) {
  std::vector<V> forces(snapshot.size());
  for (std::size_t i = 0; i < snapshot.size(); ++i)
    if (snapshot[i].mass > 0) {
      const FieldContext context{snapshot[i], snapshot};
      for (const auto &field : environment.fields) {
        const auto next = field->sample(context);
        finite(next);
        forces[i] = forces[i] + v(next);
      }
      (void)forces[i].vector();
    }
  return forces;
}
void integrate(Body &b, V force, float dt, Integration method) {
  if (!b.inverseMass)
    return;
  const double kick = method == Integration::VelocityVerlet ? 0.5 : 1;
  b.velocity = b.velocity + force * (b.inverseMass * dt * kick);
  b.shape.center = b.shape.center + b.velocity * dt;
  b.turn(b.angular * dt);
}
} // namespace Jade::Physics::Internal