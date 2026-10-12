#pragma once
#include "../CollisionWorld.hpp"
#include <Jade/Core/Transform.hpp>
#include <Jade/Physics/Environment.hpp>
#include <Jade/Physics/System.hpp>

namespace Jade::Physics::Internal {
using V = Jade::Detail::PhysicsVector;
using Detail::collisionCross;
using Detail::collisionDot;
using Detail::collisionLength;
V v(Vec3 p);
inline V v(V p) { return p; }
void finite(Vec3 p);
void validate(const RigidBody &b);
struct Body {
  Detail::WorldCollider shape;
  Rotation orientation;
  V velocity, angular, inverseInertia;
  double inverseMass = 0;
  double mass = 0;
  double friction = .6, restitution = 0;
  bool edited = true, geometryEdited = true;
  V momentum() const {
    V result;
    for (std::size_t i = 0; i < 3; ++i) if (inverseInertia[i] > 0)
      result = result + shape.axes[i] * (collisionDot(shape.axes[i],angular)/inverseInertia[i]);
    return result;
  }
  V inertia(V value) const {
    V result;
    for (std::size_t i = 0; i < 3; ++i)
      result = result + shape.axes[i] * (collisionDot(shape.axes[i], value) * inverseInertia[i]);
    return result;
  }
  V local(V p) const {
    V result;
    for (std::size_t i = 0; i < 3; ++i)
      result[i] = collisionDot(p - shape.center, shape.axes[i]);
    return result;
  }
  V offset(V p) const {
    V result;
    for (std::size_t i = 0; i < 3; ++i)
      result = result + shape.axes[i] * p[i];
    return result;
  }
  void turn(V angle) {
    if (collisionLength(angle) < 1e-12)
      return;
    const auto length = collisionLength(angle);
    const auto s = std::sin(length*.5)/length;
    const Rotation q{std::cos(length*.5),angle.x*s,angle.y*s,angle.z*s};
    orientation = (q*orientation).normalized();
    // Reconstruct from the authoritative orientation; repeated float rotations
    // of the old basis otherwise accumulate scale/shear and corrupt inertia.
    shape.axes[0] = Detail::collisionUnit(orientation.rotate({1,0,0}));
    auto y = orientation.rotate({0,1,0});
    shape.axes[1] = Detail::collisionUnit(y-shape.axes[0]*collisionDot(y,shape.axes[0]));
    shape.axes[2] = Detail::collisionUnit(collisionCross(shape.axes[0],shape.axes[1]));
  }
};
Body body(const Jade::World &scene, const Detail::WorldCollider &shape);
std::vector<FieldBody> fieldBodies(std::span<const Body> bodies);
std::vector<V> sampleForces(const Environment &environment, std::span<const FieldBody> snapshot, double time = 0);
void integrateOrientation(Body& body, double dt);
double angularSpeedBound(const Body& body);
} // namespace Jade::Physics::Internal
