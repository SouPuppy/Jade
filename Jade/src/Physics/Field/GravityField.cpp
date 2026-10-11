#include "../../internal/Physics/Math.hpp"
#include <Jade/Core/Scene.hpp>
#include <Jade/Core/Transform.hpp>
#include <Jade/Physics/Collider.hpp>
#include <Jade/Physics/Environment.hpp>
#include <Jade/Physics/RigidBody.hpp>
#include <algorithm>

namespace Jade::Physics {
namespace {
using V = Detail::PhysicsVector;
V vector(Vec3 p) {
  return {p.x, p.y, p.z};
}
void finite(Vec3 p) {
  (void)vector(p).vector();
}
void parameters(double mass, double g, double softening) {
  if (!std::isfinite(mass) || mass < 0 || !std::isfinite(g) || g < 0 || !std::isfinite(softening) ||
      softening <= 0)
    throw std::invalid_argument("Gravity mass/G must be finite and nonnegative; softening must be positive.");
}
V point(Vec3 center, Vec3 position, double mass, double g, double softening) {
  const auto r = vector(center) - vector(position);
  if (mass == 0 || g == 0 || Detail::collisionLength(r) == 0)
    return {};
  const double distance = std::hypot(Detail::collisionLength(r), softening);
  // Normalize first to avoid overflow in the cubed distance.
  return (r * (1 / distance)) * ((g / distance) * (mass / distance));
}
} // namespace
Vec3 GravityField::sample(const FieldContext &c) const {
  const auto a = acceleration(c);
  finite(a);
  return (vector(a) * c.body.mass).vector();
}
UniformGravity::UniformGravity(Vec3 acceleration) : m_Acceleration(acceleration) {
  finite(acceleration);
}
Vec3 UniformGravity::acceleration(const FieldContext &) const {
  return m_Acceleration;
}
PointGravity::PointGravity(Vec3 center, double mass, double g, double softening)
    : m_Center(center), m_Mass(mass), m_G(g), m_Softening(softening) {
  finite(center);
  parameters(mass, g, softening);
}
Vec3 PointGravity::acceleration(const FieldContext &c) const {
  return point(m_Center, c.body.position, m_Mass, m_G, m_Softening).vector();
}
CompositeGravity::CompositeGravity(std::vector<std::shared_ptr<const GravityField>> fields)
    : m_Fields(std::move(fields)) {
  for (const auto &field : m_Fields)
    if (!field)
      throw std::invalid_argument("Gravity child cannot be null.");
}
Vec3 CompositeGravity::acceleration(const FieldContext &c) const {
  V sum;
  for (const auto &field : m_Fields) {
    const auto next = field->acceleration(c);
    finite(next);
    sum = sum + vector(next);
  }
  return sum.vector();
}
void CompositeGravity::validate(const Scene &scene) const {
  for (const auto &field : m_Fields)
    field->validate(scene);
}
BodyGravity::BodyGravity(std::vector<Entity> sources, double g, double softening)
    : m_Sources(std::move(sources)), m_G(g), m_Softening(softening) {
  parameters(0, g, softening);
  for (std::size_t i = 0; i < m_Sources.size(); ++i)
    if (std::find(m_Sources.begin(), m_Sources.begin() + i, m_Sources[i]) != m_Sources.begin() + i)
      throw std::invalid_argument("Gravity sources must be unique.");
}
void BodyGravity::validate(const Scene &scene) const {
  for (const auto source : m_Sources) {
    if (!source.valid())
      continue;
    if (!scene.owns(source) || !source.has<Transform>() || !source.has<Collider>() ||
        source.get<Collider>().empty() || !source.has<RigidBody>() || scene.parent(source))
      throw std::invalid_argument("Gravity source must be a root dynamic body in this Scene.");
    const float mass = source.get<RigidBody>().mass;
    if (!std::isfinite(mass) || mass <= 0)
      throw std::invalid_argument("Gravity source mass must be positive and finite.");
  }
}
Vec3 BodyGravity::acceleration(const FieldContext &c) const {
  V sum;
  for (const auto source : m_Sources) {
    if (source == c.body.entity)
      continue;
    const auto found = std::find_if(c.bodies.begin(), c.bodies.end(),
                                    [&](const FieldBody &b) { return b.entity == source; });
    if (found != c.bodies.end())
      sum = sum + point(found->position, c.body.position, found->mass, m_G, m_Softening);
  }
  return sum.vector();
}
void Environment::validate(const Scene &scene) const {
  if (integration != Integration::SemiImplicitEuler && integration != Integration::VelocityVerlet)
    throw std::invalid_argument("Unknown Physics integration method.");
  for (const auto &field : fields) {
    if (!field)
      throw std::invalid_argument("Force field cannot be null.");
    field->validate(scene);
  }
}
} // namespace Jade::Physics
