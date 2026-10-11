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
void finite(Vec3 p);
void validate(const RigidBody &b);
Quaternion rotation(V angle);
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
    const auto q = rotation(angle);
    pose.rotation = (q * pose.rotation).normalized();
    for (auto &axis : shape.axes)
      axis = v(q.rotate(axis.vector()));
  }
};
Body body(const Scene &scene, const Detail::WorldCollider &shape);
bool sameGeometry(const Body &a, const Body &b);
bool same(const Body &a, const Body &b);
std::vector<FieldBody> fieldBodies(std::span<const Body> bodies);
std::vector<V> sampleForces(const Environment &environment, std::span<const FieldBody> snapshot);
void integrate(Body &body, V force, float dt, Integration method);
} // namespace Jade::Physics::Internal
