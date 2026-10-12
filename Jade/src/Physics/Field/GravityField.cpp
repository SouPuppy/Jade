#include "../../internal/Physics/Math.hpp"
#include <Jade/World/World.hpp>
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
V precisePoint(V center,V position,double mass,double g,double softening) {
  const auto r = center-position; const auto distance = std::hypot(Detail::collisionLength(r),softening);
  return (r*(1/distance))*((g/distance)*(mass/distance));
}
} // namespace
const FieldBody* FieldContext::find(Entity entity) const {
  if (!entityIndex.empty()) {
    const auto it = std::lower_bound(entityIndex.begin(),entityIndex.end(),entity.id(),
      [](const auto& entry, auto key) { return entry.first < key; });
    return it != entityIndex.end() && it->first == entity.id() && bodies[it->second].entity == entity ? &bodies[it->second] : nullptr;
  }
  const auto it = std::find_if(bodies.begin(),bodies.end(),[&](const auto& b) { return b.entity == entity; });
  return it == bodies.end() ? nullptr : &*it;
}
const FieldBody* FieldContext::find(BodyID id) const {
  // World snapshots use stable slot order, including disabled/tombstone entries.
  if (id.slot < bodies.size() && bodies[id.slot].id == id) return &bodies[id.slot];
  return nullptr;
}
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
FieldSample UniformGravity::sampleStage(const FieldContext& c) const { return {vector(m_Acceleration)*c.body.mass,{}}; }
PointGravity::PointGravity(Vec3 center, double mass, double g, double softening)
    : m_Center(center), m_Mass(mass), m_G(g), m_Softening(softening) {
  finite(center);
  parameters(mass, g, softening);
}
Vec3 PointGravity::acceleration(const FieldContext &c) const {
  return point(m_Center, c.body.position, m_Mass, m_G, m_Softening).vector();
}
FieldSample PointGravity::sampleStage(const FieldContext& c) const {
  const auto r = vector(m_Center)-(c.body.id ? c.body.precisePosition : vector(c.body.position));
  const double d = std::hypot(Detail::collisionLength(r),m_Softening);
  return {(r*(1/d))*((m_G/d)*(m_Mass/d)*c.body.mass),{}};
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
void CompositeGravity::validate(const Jade::World &scene) const {
  for (const auto &field : m_Fields)
    field->validate(scene);
}
bool CompositeGravity::permitsSleep() const noexcept {
  return std::all_of(m_Fields.begin(),m_Fields.end(),[](const auto& field) { return field->permitsSleep(); });
}
FieldSample CompositeGravity::sampleStage(const FieldContext& context) const {
  FieldSample sum;
  for (const auto& field : m_Fields) { const auto next = field->sampleStage(context); sum.force = sum.force+next.force; sum.torque = sum.torque+next.torque; }
  return sum;
}
ForceField::Kernel CompositeGravity::prepare(std::span<const FieldBody> bodies,std::span<const std::pair<std::uint64_t,std::size_t>> index) const {
  std::vector<Kernel> kernels; kernels.reserve(m_Fields.size());
  for (const auto& field : m_Fields) kernels.push_back(field->prepare(bodies,index));
  return [kernels = std::move(kernels)](const FieldContext& context) {
    FieldSample sum; for (const auto& kernel : kernels) { const auto next = kernel(context); sum.force = sum.force+next.force; sum.torque = sum.torque+next.torque; } return sum;
  };
}
BodyGravity::BodyGravity(std::vector<BodyID> sources, double g, double softening)
    : m_BodySources(std::move(sources)), m_G(g), m_Softening(softening) {
  parameters(0,g,softening);
  for (std::size_t i = 0; i < m_BodySources.size(); ++i)
    if (!m_BodySources[i] || std::find(m_BodySources.begin(),m_BodySources.begin()+i,m_BodySources[i]) != m_BodySources.begin()+i)
      throw std::invalid_argument("Gravity sources must be unique live IDs.");
}
BodyGravity::BodyGravity(std::vector<Entity> sources, double g, double softening)
    : m_Sources(std::move(sources)), m_G(g), m_Softening(softening) {
  parameters(0, g, softening);
  for (std::size_t i = 0; i < m_Sources.size(); ++i)
    if (std::find(m_Sources.begin(), m_Sources.begin() + i, m_Sources[i]) != m_Sources.begin() + i)
      throw std::invalid_argument("Gravity sources must be unique.");
}
void BodyGravity::validate(const Jade::World &scene) const {
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
    const auto found = c.find(source);
    if (found)
      sum = sum + point(found->position, c.body.position, found->mass, m_G, m_Softening);
  }
  for (const auto source : m_BodySources) {
    if (source == c.body.id) continue;
    const auto found = c.find(source);
    if (!found || found->mass <= 0) throw std::invalid_argument("Gravity source is foreign, disabled or stale.");
    sum = sum + point(found->position,c.body.position,found->mass,m_G,m_Softening);
  }
  return sum.vector();
}
FieldSample BodyGravity::sampleStage(const FieldContext& c) const {
  V acceleration;
  const auto position = c.body.id ? c.body.precisePosition : vector(c.body.position);
  for (const auto source : m_Sources) {
    if (source == c.body.entity) continue;
    if (const auto b = c.find(source)) acceleration = acceleration+precisePoint(b->id ? b->precisePosition : vector(b->position),position,b->mass,m_G,m_Softening);
  }
  for (const auto source : m_BodySources) {
    if (source == c.body.id) continue;
    const auto b = c.find(source);
    if (!b || b->mass <= 0) throw std::invalid_argument("Gravity source is foreign, disabled or stale.");
    acceleration = acceleration+precisePoint(b->precisePosition,position,b->mass,m_G,m_Softening);
  }
  return {acceleration*c.body.mass,{}};
}
ForceField::Kernel BodyGravity::prepare(std::span<const FieldBody> bodies,std::span<const std::pair<std::uint64_t,std::size_t>> index) const {
  const FieldBody empty; const FieldContext context{empty,bodies,index}; std::vector<std::size_t> sources;
  sources.reserve(m_Sources.size()+m_BodySources.size());
  for (const auto entity : m_Sources) if (const auto b = context.find(entity)) sources.push_back(static_cast<std::size_t>(b-bodies.data()));
  for (const auto id : m_BodySources) {
    const auto b = context.find(id); if (!b || b->mass <= 0) throw std::invalid_argument("Gravity source is foreign, disabled or stale.");
    sources.push_back(static_cast<std::size_t>(b-bodies.data()));
  }
  const auto g = m_G,softening = m_Softening;
  return [sources = std::move(sources),g,softening](const FieldContext& c) {
    V acceleration; const auto p = c.body.id ? c.body.precisePosition : vector(c.body.position);
    for (const auto i : sources) {
      const auto& source = c.bodies[i]; if (&source == &c.body || (c.body.id && source.id == c.body.id) ||
          (c.body.entity.id() != 0 && source.entity == c.body.entity)) continue;
      acceleration = acceleration+precisePoint(source.id ? source.precisePosition : vector(source.position),p,source.mass,g,softening);
    }
    return FieldSample{acceleration*c.body.mass,{}};
  };
}
void Environment::validate(const Jade::World &scene) const {
  if (integration != Integration::SemiImplicitEuler && integration != Integration::VelocityVerlet)
    throw std::invalid_argument("Unknown Physics integration method.");
  for (const auto &field : fields) {
    if (!field)
      throw std::invalid_argument("Force field cannot be null.");
    field->validate(scene);
  }
}
} // namespace Jade::Physics
