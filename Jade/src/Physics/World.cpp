#include "../internal/Physics/WorldImpl.hpp"
#include "../internal/Physics/Manifold.hpp"
#include <type_traits>

namespace Jade::Physics {
namespace {
std::atomic<std::uint64_t> nextWorld{1};
using namespace Internal;
void checkVector(Vector p) { for (unsigned k = 0; k < 3; ++k) if (!std::isfinite(p[k])) throw std::invalid_argument("Physics vector must be finite."); }
}
namespace Internal {
void validateConfig(const Config& c) {
  if (!std::isfinite(c.fixedStep) || c.fixedStep <= 0 || !c.maxSubsteps || !c.velocityIterations || !c.positionIterations ||
      !std::isfinite(c.contactDistance) || c.contactDistance <= 0 || !std::isfinite(c.penetrationSlop) || c.penetrationSlop < 0 || c.contactDistance < c.penetrationSlop ||
      !std::isfinite(c.sleepVelocity) || c.sleepVelocity < 0 || !std::isfinite(c.sleepTime) || c.sleepTime <= 0 ||
      !c.maxToiEvents || !c.maxToiIterations || !c.parallelThreshold)
    throw std::invalid_argument("Invalid Physics configuration.");
}
void validateDescription(const BodyDescription& d) {
  checkVector(d.position); checkVector(d.velocity); checkVector(d.angularVelocity); (void)d.rotation.normalized();
  if (d.motion != MotionType::Static && d.motion != MotionType::Dynamic && d.motion != MotionType::Kinematic)
    throw std::invalid_argument("Invalid body motion type.");
  if (!std::isfinite(d.mass) || d.mass < 0 || (d.motion == MotionType::Dynamic && d.mass == 0) ||
      !std::isfinite(d.friction) || d.friction < 0 || !std::isfinite(d.restitution) || d.restitution < 0 || d.restitution > 1 ||
      !std::isfinite(d.linearDamping) || d.linearDamping < 0 || !std::isfinite(d.angularDamping) || d.angularDamping < 0 ||
      (d.collision != CollisionMode::Discrete && d.collision != CollisionMode::Continuous))
    throw std::invalid_argument("Invalid body mass, material or damping.");
  if (d.shape.empty()) return;
  if (Detail::ColliderAccess::kind(d.shape) == Detail::CollisionKind::Sphere) {
    const auto r = Detail::ColliderAccess::sphere(d.shape).radius;
    if (!std::isfinite(r) || r <= 0) throw std::invalid_argument("Sphere radius must be positive.");
  } else {
    const auto h = v(Detail::ColliderAccess::box(d.shape).halfExtents);
    for (unsigned k = 0; k < 3; ++k) if (!std::isfinite(h[k]) || h[k] <= 0) throw std::invalid_argument("Box extents must be positive.");
  }
}
}
World::Impl::Impl(Config c) : identity(nextWorld.fetch_add(1)),config(c) {
  validateConfig(c); fields.push_back({std::make_shared<const UniformGravity>()});
}
World::Impl::Slot& World::Impl::require(BodyID body) {
  if (body.world != identity || body.slot >= slots.size() || !slots[body.slot].alive || slots[body.slot].generation != body.generation)
    throw std::invalid_argument("BodyID is foreign or stale.");
  return slots[body.slot];
}
const World::Impl::Slot& World::Impl::require(BodyID body) const { return const_cast<Impl*>(this)->require(body); }
Body World::Impl::makeBody(const BodyDescription& d) {
  Body b; (void)d.position.vector(); b.orientation = d.rotation.normalized();
  b.mass = d.motion == MotionType::Dynamic ? d.mass : 0;
  b.friction = d.friction; b.restitution = d.restitution;
  b.velocity = d.motion == MotionType::Static ? V{} : d.velocity;
  b.angular = d.motion == MotionType::Static ? V{} : d.angularVelocity;
  b.shape.center = d.position;
  b.shape.axes = {b.orientation.rotate({1,0,0}),b.orientation.rotate({0,1,0}),b.orientation.rotate({0,0,1})};
  b.shape.kind = d.shape.empty() ? Detail::CollisionKind::Sphere : Detail::ColliderAccess::kind(d.shape);
  if (!d.shape.empty() && b.shape.kind == Detail::CollisionKind::Sphere) b.shape.radius = Detail::ColliderAccess::sphere(d.shape).radius;
  if (!d.shape.empty() && b.shape.kind == Detail::CollisionKind::Box) b.shape.halfExtents = v(Detail::ColliderAccess::box(d.shape).halfExtents);
  if (d.motion == MotionType::Dynamic && !d.shape.empty() && d.enabled) {
    b.inverseMass = 1/d.mass;
    if (b.shape.kind == Detail::CollisionKind::Sphere) {
      const double inverse = 2.5*b.inverseMass/(b.shape.radius*b.shape.radius); b.inverseInertia = {inverse,inverse,inverse};
    } else for (unsigned k = 0; k < 3; ++k) {
      const auto h = b.shape.halfExtents; const auto a = h[(k+1)%3],c = h[(k+2)%3]; b.inverseInertia[k] = 3*b.inverseMass/(a*a+c*c);
    }
    if (!std::isfinite(b.inverseMass) || b.inverseMass <= 0) throw std::invalid_argument("Body mass exceeds the supported inertia range.");
    for (unsigned k = 0; k < 3; ++k) if (!std::isfinite(b.inverseInertia[k]) || b.inverseInertia[k] <= 0)
      throw std::invalid_argument("Body dimensions/mass exceed the supported inertia range.");
  }
  checkVector(b.momentum());
  Detail::updateBounds(b.shape); (void)b.shape.bounds.minimum.vector(); (void)b.shape.bounds.maximum.vector();
  return b;
}
Body World::Impl::makeBody(std::size_t i) const {
  auto b = slots[i].prototype; b.shape.center = StateStorage::get(state.position,i);
  b.velocity = StateStorage::get(state.velocity,i); b.angular = StateStorage::get(state.angular,i); b.orientation = state.rotation[i];
  b.shape.entity = slots[i].entity; b.edited = slots[i].edited; b.geometryEdited = slots[i].geometryEdited; return b;
}
World::World(Config c) : m_Impl(std::make_unique<Impl>(c)) {}
World::World(JobSystem& jobs,Config c) : World(c) { m_Impl->jobs = &jobs; }
World::~World() = default;
void World::setJobSystem(JobSystem* jobs) { m_Impl->writable(); m_Impl->jobs = jobs; }
void World::configure(Config c) { m_Impl->writable(); validateConfig(c); m_Impl->config = c; m_Impl->wakeAll = true; }
const Config& World::config() const noexcept { return m_Impl->config; }
void World::reserve(std::size_t n) { m_Impl->writable(); m_Impl->slots.reserve(n); m_Impl->state.reserve(n); m_Impl->work.reserve(n); }
bool World::contains(BodyID id) const noexcept {
  return id.world == m_Impl->identity && id.slot < m_Impl->slots.size() && m_Impl->slots[id.slot].alive && m_Impl->slots[id.slot].generation == id.generation;
}
BodyID World::create(const BodyDescription& d) {
  m_Impl->writable(); validateDescription(d); const auto b = Impl::makeBody(d);
  auto& impl = *m_Impl;
  impl.dirty = true;
  if (impl.stats.sleepingBodies) impl.wakeRegions.push_back(b.shape.bounds);
  std::uint32_t i;
  if (impl.freeSlots.empty()) {
    if (impl.slots.size() >= std::numeric_limits<std::uint32_t>::max()) throw std::length_error("Too many physics bodies.");
    i = static_cast<std::uint32_t>(impl.slots.size()); impl.state.resize(i+1); impl.slots.emplace_back();
  } else { i = impl.freeSlots.back(); impl.freeSlots.pop_back(); }
  auto& slot = impl.slots[i]; const auto generation = slot.generation; slot = {}; slot.generation = generation; slot.alive = true; slot.description = d; slot.prototype = b;
  Impl::StateStorage::put(impl.state.position,i,d.position); Impl::StateStorage::put(impl.state.velocity,i,b.velocity);
  Impl::StateStorage::put(impl.state.angular,i,b.angular); Impl::StateStorage::put(impl.state.momentum,i,b.momentum());
  impl.state.rotation[i] = d.rotation.normalized(); impl.state.sleeping[i] = 0; impl.indexDirty = true; impl.graphDirty = true;
  return impl.id(i);
}
void World::destroy(BodyID id) {
  m_Impl->writable(); auto& s = m_Impl->require(id);
  if (m_Impl->stats.sleepingBodies) { auto shape = m_Impl->makeBody(id.slot).shape; Detail::updateBounds(shape); m_Impl->wakeRegions.push_back(shape.bounds); }
  m_Impl->freeSlots.push_back(id.slot);
  s.alive = false; ++s.generation; m_Impl->indexDirty = true; m_Impl->graphDirty = true;
  m_Impl->dirty = true;
  m_Impl->contacts.clear();
}
BodyState World::state(BodyID id) const {
  const auto& s = m_Impl->require(id); const auto& a = m_Impl->state;
  return {Impl::StateStorage::get(a.position,id.slot),Impl::StateStorage::get(a.velocity,id.slot),
    Impl::StateStorage::get(a.angular,id.slot),Impl::StateStorage::get(a.momentum,id.slot),a.rotation[id.slot],
    s.description.enabled && !s.description.shape.empty(),a.sleeping[id.slot] != 0};
}
BodyDescription World::description(BodyID id) const {
  auto d = m_Impl->require(id).description; const auto s = state(id);
  d.position = s.position; d.rotation = s.rotation; d.velocity = s.velocity; d.angularVelocity = s.angularVelocity; return d;
}
void World::Impl::invalidate(std::size_t i,bool geometry) {
  dirty = true;
  slots[i].edited = true; slots[i].geometryEdited |= geometry; slots[i].quiet = 0; slots[i].forces.status = ForceStatus::Stale;
  state.sleeping[i] = 0; indexDirty |= geometry;
  graphDirty |= geometry;
  // Wake propagation is resolved from the cached contact graph before integration.
}
void World::update(BodyID id,const BodyDescription& d) {
  m_Impl->writable(); m_Impl->require(id); validateDescription(d); const auto b = Impl::makeBody(d);
  if (m_Impl->stats.sleepingBodies) {
    auto old = m_Impl->makeBody(id.slot).shape; Detail::updateBounds(old);
    m_Impl->wakeRegions.push_back(old.bounds); m_Impl->wakeRegions.push_back(b.shape.bounds);
  }
  m_Impl->slots[id.slot].description = d;
  m_Impl->slots[id.slot].prototype = b;
  Impl::StateStorage::put(m_Impl->state.position,id.slot,d.position); Impl::StateStorage::put(m_Impl->state.velocity,id.slot,b.velocity);
  Impl::StateStorage::put(m_Impl->state.angular,id.slot,b.angular); Impl::StateStorage::put(m_Impl->state.momentum,id.slot,b.momentum());
  m_Impl->state.rotation[id.slot] = d.rotation.normalized(); m_Impl->invalidate(id.slot,true);
}
void World::setEnabled(BodyID id,bool enabled) { auto d = description(id); d.enabled = enabled; update(id,d); }
void World::teleport(BodyID id,Vector p,Rotation q,bool clearVelocity) {
  auto d = description(id); d.position = p; d.rotation = q; if (clearVelocity) { d.velocity = {}; d.angularVelocity = {}; } update(id,d);
}
void World::setVelocity(BodyID id,Vector v,Vector w) {
  m_Impl->writable(); m_Impl->require(id); checkVector(v); checkVector(w); (void)v.vector(); (void)w.vector();
  if (m_Impl->slots[id.slot].description.motion == MotionType::Static) { v = {}; w = {}; }
  auto b = m_Impl->makeBody(id.slot); b.angular = w; const auto momentum = b.momentum(); checkVector(momentum);
  Impl::StateStorage::put(m_Impl->state.velocity,id.slot,v); Impl::StateStorage::put(m_Impl->state.angular,id.slot,w);
  Impl::StateStorage::put(m_Impl->state.momentum,id.slot,momentum); m_Impl->invalidate(id.slot,false);
}
void World::addForce(BodyID id,Vector force,Vector torque) {
  m_Impl->writable(); auto& s = m_Impl->require(id); checkVector(force); checkVector(torque);
  const auto f = s.force+force, t = s.torque+torque; checkVector(f); checkVector(t); s.force = f; s.torque = t; m_Impl->invalidate(id.slot,false);
}
void World::setPersistentForce(BodyID id,Vector f,Vector t) {
  m_Impl->writable(); auto& s = m_Impl->require(id); checkVector(f); checkVector(t); s.persistentForce = f; s.persistentTorque = t; m_Impl->invalidate(id.slot,false);
}
void World::applyImpulse(BodyID id,Vector impulse,Vector point) {
  m_Impl->writable(); m_Impl->require(id); checkVector(impulse); checkVector(point); auto b = m_Impl->makeBody(id.slot);
  if (!b.inverseMass) return;
  const auto linear = b.velocity+impulse*b.inverseMass, angular = b.angular+b.inertia(collisionCross(point-b.shape.center,impulse));
  (void)linear.vector(); (void)angular.vector(); setVelocity(id,linear,angular);
}
void World::wake(BodyID id) { m_Impl->writable(); m_Impl->require(id); m_Impl->invalidate(id.slot,false); }
void World::setKinematicTarget(BodyID id,Vector p,Rotation q,double duration) {
  m_Impl->writable(); auto& s = m_Impl->require(id); checkVector(p); (void)p.vector(); q = q.normalized();
  if (s.description.motion != MotionType::Kinematic || !std::isfinite(duration) || duration <= 0) throw std::invalid_argument("Invalid kinematic target.");
  const auto current = state(id); auto delta = (q*current.rotation.conjugate()).normalized();
  if (delta.w < 0) delta = {-delta.w,-delta.x,-delta.y,-delta.z};
  const auto axis = Vector{delta.x,delta.y,delta.z}; const auto length = collisionLength(axis);
  setVelocity(id,(p-current.position)*(1/duration),length > 1e-12 ? axis*(2*std::atan2(length,delta.w)/(length*duration)) : Vector{});
  s.hasTarget = true; s.targetPosition = p; s.targetRotation = q; s.targetTime = duration;
}
FieldID World::addField(std::shared_ptr<const ForceField> field) {
  m_Impl->writable(); if (!field) throw std::invalid_argument("Null force field.");
  for (std::size_t i = 0; i < m_Impl->fields.size(); ++i) if (!m_Impl->fields[i].field) {
    m_Impl->fields[i].field = std::move(field); m_Impl->wakeAll = true;
    for (auto& s : m_Impl->slots) s.forces.status = ForceStatus::Stale;
    return {m_Impl->identity,static_cast<std::uint32_t>(i),m_Impl->fields[i].generation};
  }
  m_Impl->fields.push_back({std::move(field),++m_Impl->fieldGeneration}); m_Impl->wakeAll = true;
  for (auto& s : m_Impl->slots) s.forces.status = ForceStatus::Stale;
  return {m_Impl->identity,static_cast<std::uint32_t>(m_Impl->fields.size()-1),m_Impl->fields.back().generation};
}
void World::removeField(FieldID id) {
  m_Impl->writable();
  if (id.world != m_Impl->identity || id.slot >= m_Impl->fields.size() || id.generation != m_Impl->fields[id.slot].generation || !m_Impl->fields[id.slot].field)
    throw std::invalid_argument("FieldID is foreign or stale.");
  auto& field = m_Impl->fields[id.slot]; field.field.reset(); field.generation = ++m_Impl->fieldGeneration; m_Impl->wakeAll = true;
  for (auto& s : m_Impl->slots) s.forces.status = ForceStatus::Stale;
}
void World::setFields(std::vector<std::shared_ptr<const ForceField>> fields) {
  m_Impl->writable(); for (const auto& f : fields) if (!f) throw std::invalid_argument("Null force field.");
  bool same = fields.size() == m_Impl->fields.size();
  for (std::size_t i = 0; same && i < fields.size(); ++i) same = fields[i] == m_Impl->fields[i].field;
  if (same) return;
  std::vector<Impl::FieldSlot> next; next.reserve(fields.size());
  const auto generation = ++m_Impl->fieldGeneration;
  for (auto& f : fields) next.push_back({std::move(f),generation});
  m_Impl->fields = std::move(next); m_Impl->wakeAll = true;
  for (auto& s : m_Impl->slots) s.forces.status = ForceStatus::Stale;
}
void World::setIntegration(Integration method) {
  m_Impl->writable(); if (method != Integration::SemiImplicitEuler && method != Integration::VelocityVerlet) throw std::invalid_argument("Invalid integrator.");
  if (m_Impl->integration != method) { m_Impl->integration = method; m_Impl->wakeAll = true; for (auto& s : m_Impl->slots) s.forces.status = ForceStatus::Stale; }
}
void World::bind(BodyID id,Entity entity) {
  m_Impl->writable(); auto& slot = m_Impl->require(id);
  if (slot.entity != entity) { slot.entity = entity; m_Impl->dirty = true; }
}
std::vector<std::shared_ptr<const ForceField>> World::fields() const {
  m_Impl->writable(); std::vector<std::shared_ptr<const ForceField>> result;
  for (const auto& field : m_Impl->fields) if (field.field) result.push_back(field.field); return result;
}
std::vector<FieldBody> World::fieldSnapshot() const {
  m_Impl->writable(); std::vector<FieldBody> result(m_Impl->slots.size());
  for (std::size_t i = 0; i < result.size(); ++i) if (m_Impl->slots[i].alive) {
    const auto id = m_Impl->id(i); const auto state = this->state(id); const auto& slot = m_Impl->slots[i];
    result[i] = {slot.entity,state.position.vector(),state.velocity.vector(),
      state.enabled && slot.description.motion == MotionType::Dynamic ? slot.description.mass : 0,id,state.position,state.velocity,
      state.rotation,state.angularVelocity,state.angularMomentum};
  }
  return result;
}
double World::time() const noexcept { return m_Impl->time; }
const Statistics& World::statistics() const noexcept { return m_Impl->stats; }
std::size_t World::memoryUsage() const noexcept {
  const auto& p = *m_Impl;
  const auto bytes = [](const auto& v) { return v.capacity()*sizeof(typename std::remove_cvref_t<decltype(v)>::value_type); };
  std::size_t total = sizeof(Impl)+p.staticTree.memoryUsage()+p.movingTree.memoryUsage();
  for (const auto* arrays : {&p.state.position,&p.state.velocity,&p.state.angular,&p.state.momentum}) for (const auto& v : *arrays) total += bytes(v);
  total += bytes(p.state.rotation)+bytes(p.state.sleeping)+bytes(p.slots)+bytes(p.freeSlots)+bytes(p.fields);
  total += bytes(p.pairs)+bytes(p.nextPairs)+bytes(p.contacts)+bytes(p.nextContacts)+bytes(p.nextForces)+bytes(p.shapes)+bytes(p.predicted)+bytes(p.swept)+bytes(p.wakeRegions)+bytes(p.statics)+bytes(p.moving);
  total += bytes(p.work)+bytes(p.snapshot)+bytes(p.entityIndex)+bytes(p.samples)+bytes(p.firstSamples)+bytes(p.kernels)+bytes(p.velocityBefore)+bytes(p.contactImpulse)+bytes(p.solverVelocity)+bytes(p.solverMomentum);
  total += bytes(p.awake)+bytes(p.quiet)+bytes(p.parents)+bytes(p.islandParents)+bytes(p.reached)+bytes(p.wakeRoots)+bytes(p.stable)+bytes(p.hasContact)+bytes(p.hasIsland);
  total += bytes(p.colorMasks)+bytes(p.islandQuiet)+bytes(p.toiResults)+bytes(p.continuousBodies)+bytes(p.contactOffsets)+bytes(p.penetrationBlocks)+bytes(p.candidates)+bytes(p.batches)+bytes(p.colors)+bytes(p.pairBuffers);
  for (const auto& v : p.colors) total += bytes(v);
  for (const auto& v : p.pairBuffers) total += bytes(v);
  return total;
}
std::span<const WorldContact> World::contacts() const noexcept { return m_Impl->contacts; }
ForceState World::forces(BodyID id) const {
  auto result = m_Impl->require(id).forces; if (m_Impl->dirty || m_Impl->wakeAll) result.status = ForceStatus::Stale; return result;
}
} // namespace Jade::Physics
