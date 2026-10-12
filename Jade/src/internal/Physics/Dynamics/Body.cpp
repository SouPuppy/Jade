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
Body body(const Jade::World &scene, const Detail::WorldCollider &shape) {
  Body b;
  b.shape = shape;
  b.orientation = Rotation{shape.entity.get<Transform>().rotation}.normalized();
  if (!shape.entity.has<RigidBody>()) {
    b.mass = 0;
    return b;
  }
  const auto& properties = shape.entity.get<RigidBody>();
  b.friction = properties.friction; b.restitution = properties.restitution;
  validate(properties);
  b.mass = properties.motion == MotionType::Dynamic ? properties.mass : 0;
  if (b.mass == 0)
    return b;
  if (scene.parent(shape.entity))
    throw std::invalid_argument("Dynamic RigidBody must be a Scene root.");
  b.inverseMass = 1.0 / properties.mass;
  b.velocity = v(properties.velocity);
  b.angular = v(properties.angularVelocity);
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
std::vector<FieldBody> fieldBodies(std::span<const Body> bodies) {
  std::vector<FieldBody> snapshot;
  snapshot.reserve(bodies.size());
  for (const auto &b : bodies)
    snapshot.push_back({b.shape.entity, b.shape.center.vector(), b.velocity.vector(), b.mass,{},b.shape.center,b.velocity,b.orientation,b.angular,b.momentum()});
  return snapshot;
}
std::vector<V> sampleForces(const Environment &environment, std::span<const FieldBody> snapshot, double time) {
  std::vector<V> forces(snapshot.size());
  std::vector<std::pair<std::uint64_t,std::size_t>> index;
  for (std::size_t i = 0; i < snapshot.size(); ++i) if (snapshot[i].entity.valid()) index.emplace_back(snapshot[i].entity.id(),i);
  std::sort(index.begin(),index.end());
  std::vector<ForceField::Kernel> kernels;
  for (const auto& field : environment.fields) kernels.push_back(field->prepare(snapshot,index));
  for (std::size_t i = 0; i < snapshot.size(); ++i)
    if (snapshot[i].mass > 0) {
      const FieldContext context{snapshot[i], snapshot,index,time};
      for (const auto &kernel : kernels) {
        const auto next = kernel(context);
        forces[i] = forces[i] + next.force;
      }
      (void)forces[i].vector();
    }
  return forces;
}
double angularSpeedBound(const Body& b) {
  if (b.shape.kind == Detail::CollisionKind::Sphere) return 0;
  if (!b.inverseMass) return collisionLength(b.angular);
  const auto momentum = b.momentum();
  // A principal-axis rotation (including an isotropic body) has constant omega.
  const auto precession = collisionLength(collisionCross(momentum,b.angular));
  if (precession <= 1e-12*collisionLength(momentum)*collisionLength(b.angular)) return collisionLength(b.angular);
  // Both energy and momentum are invariants of free rotation. Use the tighter bound.
  const auto maximum = std::max({b.inverseInertia.x,b.inverseInertia.y,b.inverseInertia.z});
  return std::min(collisionLength(momentum)*maximum,std::sqrt(std::max(0.0,collisionDot(momentum,b.angular)*maximum)));
}
void integrateOrientation(Body& b,double dt) {
  if (collisionLength(b.angular) == 0) return;
  if (!b.inverseMass || b.shape.kind == Detail::CollisionKind::Sphere) { b.turn(b.angular*dt); return; }
  const auto momentum = b.momentum(); const Body initial = b; auto omega = b.angular;
  bool converged = false;
  for (unsigned iteration = 0; iteration < 32; ++iteration) {
    Body midpoint = initial; midpoint.turn(omega*(dt*.5)); const auto next = midpoint.inertia(momentum);
    if (collisionLength(next-omega)*dt < 1e-11) { omega = next; converged = true; break; }
    omega = (omega+next)*.5;
  }
  if (!converged) throw std::runtime_error("Angular midpoint integration failed to converge.");
  b = initial; b.turn(omega*dt); b.angular = b.inertia(momentum);
}
} // namespace Jade::Physics::Internal
