#include <Jade/Physics/System.hpp>
#include <Jade/Physics/World.hpp>
#include "../Core/Scene.hpp"
#include "../internal/Core/SceneTransforms.hpp"
#include "../internal/Physics/WorldImpl.hpp"
#include "../internal/Physics/Manifold.hpp"
#include "../internal/Physics/BroadPhase.hpp"

namespace Jade::Physics {
namespace {
using namespace Internal;
bool samePose(const Transform& a,const Transform& b) { return a.position == b.position && a.rotation == b.rotation && a.scale == b.scale; }
bool sameSettings(const RigidBody& a,const RigidBody& b) {
  return a.mass == b.mass && a.friction == b.friction && a.restitution == b.restitution && a.motion == b.motion && a.collision == b.collision &&
    a.allowSleep == b.allowSleep && a.linearDamping == b.linearDamping && a.angularDamping == b.angularDamping && a.category == b.category && a.mask == b.mask;
}
bool sameShape(const Detail::WorldCollider& a,const Detail::WorldCollider& b) {
  if (a.kind != b.kind || a.radius != b.radius || collisionLength(a.halfExtents-b.halfExtents) > 1e-5) return false;
  return true;
}
bool sameWorldShape(const Detail::WorldCollider& a,const Detail::WorldCollider& b) {
  if (!sameShape(a,b) || collisionLength(a.center-b.center) > 1e-5) return false;
  for (unsigned k = 0; k < 3; ++k) if (collisionLength(a.axes[k]-b.axes[k]) > 1e-5) return false;
  return true;
}
BodyDescription describe(const Scene& scene,const Detail::WorldCollider& shape,Quaternion rotation) {
  const auto entity = shape.entity; BodyDescription d; d.position = shape.center; d.rotation = rotation;
  d.shape = shape.kind == Detail::CollisionKind::Sphere ? Collider{Sphere{static_cast<float>(shape.radius)}} : Collider{Box{shape.halfExtents.vector()}};
  if (!entity.has<RigidBody>()) { d.motion = MotionType::Static; return d; }
  const auto& rb = entity.get<RigidBody>(); validate(rb);
  d.motion = rb.mass == 0 && rb.motion == MotionType::Dynamic ? MotionType::Static : rb.motion;
  if (d.motion != MotionType::Static && scene.parent(entity)) throw std::invalid_argument("Moving physics bodies require a Scene root.");
  d.mass = rb.mass; d.velocity = v(rb.velocity); d.angularVelocity = v(rb.angularVelocity);
  d.friction = rb.friction; d.restitution = rb.restitution; d.allowSleep = rb.allowSleep;
  d.linearDamping = rb.linearDamping; d.angularDamping = rb.angularDamping; d.collision = rb.collision; d.category = rb.category; d.mask = rb.mask;
  validateDescription(d); return d;
}
}
struct System::Impl {
  Config config;
  JobSystem* jobs = nullptr;
  const Scene* current = nullptr;
  Statistics stats;
  std::vector<Manifold> contacts;
  std::vector<std::shared_ptr<const ForceField>> fields;
  Integration integration = Integration::SemiImplicitEuler;
  explicit Impl(Config c) : config(c) { validateConfig(c); }
};
System::System(Config c) : m_Impl(std::make_unique<Impl>(c)) {}
System::System(JobSystem& jobs,Config c) : System(c) { m_Impl->jobs = &jobs; }
System::~System() = default;
const Config& System::config() const noexcept { return m_Impl->config; }
const Statistics& System::statistics() const noexcept { return m_Impl->stats; }
std::span<const Manifold> System::contacts() const noexcept { return m_Impl->contacts; }
void System::reset() noexcept { m_Impl->current = nullptr; m_Impl->stats = {}; m_Impl->contacts.clear(); m_Impl->fields.clear(); }
void System::synchronize(Scene& scene) {
  scene.physics().validate(scene);
  for (auto e : scene.query<RigidBody>()) {
    if (e.has<Collider>() && e.get<Collider>().empty()) continue;
    validate(e.get<RigidBody>());
    if ((e.get<RigidBody>().mass > 0 || e.get<RigidBody>().motion == MotionType::Kinematic) && (!e.has<Transform>() || !e.has<Collider>()))
      throw std::invalid_argument("Moving RigidBody requires Transform and Collider.");
  }
  // Resolve and validate the complete authoring input before issuing World edits.
  const auto shapes = Detail::captureColliders(scene); Detail::SceneTransforms transforms(scene);
  std::vector<BodyDescription> descriptions; descriptions.reserve(shapes.size());
  for (const auto& shape : shapes) descriptions.push_back(describe(scene,shape,
    scene.parent(shape.entity) ? transforms.Resolve(shape.entity).rotation : shape.entity.get<Transform>().rotation.normalized()));
  auto& world = scene.physicsWorld(); world.setJobSystem(m_Impl->jobs);
  if (!(world.config() == m_Impl->config)) world.configure(m_Impl->config);
  auto& bindings = scene.m_Impl->physicsBindings;
  std::vector<Scene::Impl::PhysicsBinding> next; next.reserve(shapes.size()); world.reserve(shapes.size());
  for (std::size_t i = 0; i < shapes.size(); ++i) {
    const auto& shape = shapes[i]; const auto e = shape.entity; const auto& pose = e.get<Transform>();
    auto it = std::lower_bound(bindings.begin(),bindings.end(),e.id(),[](const auto& b,auto id) { return b.entity.id() < id; });
    Scene::Impl::PhysicsBinding binding;
    if (it == bindings.end() || it->entity != e || !world.contains(it->body)) {
      binding.entity = e; binding.body = world.create(descriptions[i]); world.bind(binding.body,e);
    } else {
      binding = *it;
      const auto rb = e.has<RigidBody>() ? e.get<RigidBody>() : RigidBody{};
      const bool settingsChanged = binding.rigid != e.has<RigidBody>() || (e.has<RigidBody>() && !sameSettings(rb,binding.authored));
      if (settingsChanged || !sameShape(shape,binding.shape)) {
        auto d = descriptions[i]; const auto state = world.state(binding.body);
        if (samePose(pose,binding.published)) { d.position = state.position; d.rotation = state.rotation; }
        if (e.has<RigidBody>() && rb.velocity == binding.authored.velocity) d.velocity = state.velocity;
        if (e.has<RigidBody>() && rb.angularVelocity == binding.authored.angularVelocity) d.angularVelocity = state.angularVelocity;
        world.update(binding.body,d);
      } else {
        if (!samePose(pose,binding.published) || (descriptions[i].motion == MotionType::Static && !sameWorldShape(shape,binding.shape)))
          world.teleport(binding.body,descriptions[i].position,descriptions[i].rotation);
        if (e.has<RigidBody>() && (rb.velocity != binding.authored.velocity || rb.angularVelocity != binding.authored.angularVelocity))
          world.setVelocity(binding.body,v(rb.velocity),v(rb.angularVelocity));
        if (!world.state(binding.body).enabled) world.setEnabled(binding.body,true);
      }
    }
    binding.published = pose; binding.rigid = e.has<RigidBody>(); if (binding.rigid) binding.authored = e.get<RigidBody>(); binding.shape = shape;
    next.push_back(binding);
  }
  // Empty colliders suspend an existing binding, preserving the runtime state.
  const auto activeCount = next.size();
  for (const auto& b : bindings) {
    const auto it = std::lower_bound(next.begin(),next.begin()+activeCount,b.entity.id(),[](const auto& x,auto id) { return x.entity.id() < id; });
    if (it != next.begin()+activeCount && it->entity == b.entity) continue;
    if (b.entity.valid() && b.entity.has<Collider>() && b.entity.get<Collider>().empty()) {
      if (world.contains(b.body) && world.state(b.body).enabled) world.setEnabled(b.body,false);
      next.push_back(b);
    } else if (world.contains(b.body)) world.destroy(b.body);
  }
  std::sort(next.begin(),next.end(),[](const auto& a,const auto& b) { return a.entity.id() < b.entity.id(); }); bindings.swap(next);
  if (scene.m_Impl->physicsFields != scene.physics().fields) {
    world.setFields(scene.physics().fields); scene.m_Impl->physicsFields = scene.physics().fields;
  }
  if (scene.m_Impl->physicsIntegration != scene.physics().integration) {
    world.setIntegration(scene.physics().integration); scene.m_Impl->physicsIntegration = scene.physics().integration;
  }
}
void System::advance(Scene& scene,double dt) { scene.physicsWorld().step(dt); }
void System::publish(Scene& scene) {
  auto& world = scene.physicsWorld(); auto& bindings = scene.m_Impl->physicsBindings;
  for (auto& b : bindings) {
    if (!b.entity.valid() || !world.contains(b.body)) continue;
    const auto state = world.state(b.body); if (!state.enabled) continue;
    const auto d = world.description(b.body);
    if (d.motion != MotionType::Static) {
      auto& pose = b.entity.get<Transform>(); pose.position = state.position.vector(); pose.rotation = state.rotation;
      auto& rb = b.entity.get<RigidBody>(); rb.velocity = state.velocity.vector(); rb.angularVelocity = state.angularVelocity.vector();
      b.authored = rb;
    }
    b.published = b.entity.get<Transform>(); b.shape.center = state.position;
    b.shape.axes = {v(state.rotation.rotate({1,0,0})),v(state.rotation.rotate({0,1,0})),v(state.rotation.rotate({0,0,1}))}; Detail::updateBounds(b.shape);
  }
  std::vector<Entity> entities;
  for (const auto& b : bindings) { if (entities.size() <= b.body.slot) entities.resize(b.body.slot+1); entities[b.body.slot] = b.entity; }
  std::vector<Manifold> contacts; contacts.reserve(world.contacts().size());
  for (const auto& c : world.contacts()) {
    if (c.a.slot >= entities.size() || c.b.slot >= entities.size()) continue;
    Manifold m{entities[c.a.slot],entities[c.b.slot],c.normal.vector(),{}};
    for (unsigned i = 0; i < c.count; ++i) {
      const auto& p = c.points[i]; m.points.push_back({p.pointA.vector(),p.pointB.vector(),static_cast<float>(p.separation),static_cast<float>(p.normalImpulse),p.frictionImpulse.vector()});
    }
    if (m.a.id() > m.b.id()) { std::swap(m.a,m.b); m.normal = -m.normal; for (auto& p : m.points) { std::swap(p.pointA,p.pointB); p.frictionImpulse = -p.frictionImpulse; } }
    contacts.push_back(m);
  }
  std::sort(contacts.begin(),contacts.end(),[](const auto& a,const auto& b) { return a.a.id() == b.a.id() ? a.b.id() < b.b.id() : a.a.id() < b.a.id(); });
  m_Impl->contacts.swap(contacts); m_Impl->stats = world.statistics(); m_Impl->current = &scene;
  m_Impl->fields = scene.physics().fields; m_Impl->integration = scene.physics().integration;
}
void System::step(Scene& scene,float dt) {
  if (!std::isfinite(dt) || dt <= 0) throw std::invalid_argument("Physics dt must be positive and finite.");
  synchronize(scene); advance(scene,dt); publish(scene);
}
void System::applyImpulse(Scene& scene,Entity e,Vec3 impulse,Vec3 point) {
  finite(impulse); finite(point);
  if (!scene.owns(e) || !e.has<Transform>() || !e.has<RigidBody>() || !e.has<Collider>() || e.get<Collider>().empty()) throw std::invalid_argument("Impulse requires a live physical entity.");
  synchronize(scene); const auto id = scene.physicsBody(e); scene.physicsWorld().applyImpulse(id,v(impulse),v(point));
  const auto state = scene.physicsWorld().state(id); auto& rb = e.get<RigidBody>(); rb.velocity = state.velocity.vector(); rb.angularVelocity = state.angularVelocity.vector();
  for (auto& binding : scene.m_Impl->physicsBindings) if (binding.entity == e) binding.authored = rb;
}
std::vector<Manifold> System::contactSnapshot(const Scene& scene) const {
  const auto shapes = Detail::captureColliders(scene); const auto& bindings = scene.m_Impl->physicsBindings;
  bool current = m_Impl->current == &scene;
  std::size_t live = 0;
  for (const auto& b : bindings) if (scene.m_Impl->physicsWorld && scene.m_Impl->physicsWorld->contains(b.body) && scene.m_Impl->physicsWorld->state(b.body).enabled) ++live;
  current &= live == shapes.size();
  for (const auto& shape : shapes) {
    const auto it = std::lower_bound(bindings.begin(),bindings.end(),shape.entity.id(),[](const auto& b,auto id) { return b.entity.id() < id; });
    if (it == bindings.end() || it->entity != shape.entity || !samePose(it->published,shape.entity.get<Transform>()) || !sameWorldShape(it->shape,shape)) { current = false; break; }
  }
  if (!current) {
    std::vector<Manifold> result;
    for (const auto pair : Detail::broadPhase(shapes,m_Impl->config.contactDistance)) {
      auto a = body(scene,shapes[pair.a]),b = body(scene,shapes[pair.b]); const auto margin = a.inverseMass || b.inverseMass ? m_Impl->config.contactDistance : 0;
      if (auto hit = Detail::collideManifold(a.shape,b.shape,margin)) { hit->points = persistentPoints(a,b,*hit,{},margin); result.push_back(*hit); }
    }
    return result;
  }
  auto result = m_Impl->contacts;
  for (const auto p : Detail::broadPhase(shapes,0)) {
    const auto staticBody = [&](auto i) { return !shapes[i].entity.has<RigidBody>() || shapes[i].entity.get<RigidBody>().mass == 0; };
    if (staticBody(p.a) && staticBody(p.b)) if (auto hit = Detail::collideManifold(shapes[p.a],shapes[p.b],0)) result.push_back(*hit);
  }
  return result;
}
std::vector<BodyForceSnapshot> System::forceSnapshot(const Scene& scene) const {
  scene.physics().validate(scene); const auto shapes = Detail::captureColliders(scene);
  std::vector<Body> bodies; bodies.reserve(shapes.size()); for (const auto& s : shapes) bodies.push_back(body(scene,s));
  const auto samples = sampleForces(scene.physics(),fieldBodies(bodies));
  std::vector<BodyForceSnapshot> result;
  for (std::size_t i = 0; i < bodies.size(); ++i) if (bodies[i].inverseMass) {
    const auto e = shapes[i].entity; BodyForceSnapshot f{e,shapes[i].center.vector(),samples[i].vector()};
    const auto& bindings = scene.m_Impl->physicsBindings;
    const auto it = std::lower_bound(bindings.begin(),bindings.end(),e.id(),[](const auto& b,auto id) { return b.entity.id() < id; });
    if (m_Impl->current == &scene && m_Impl->fields == scene.physics().fields && m_Impl->integration == scene.physics().integration && it != bindings.end() && it->entity == e &&
        samePose(it->published,e.get<Transform>()) && sameSettings(it->authored,e.get<RigidBody>()) && it->authored.velocity == e.get<RigidBody>().velocity && it->authored.angularVelocity == e.get<RigidBody>().angularVelocity &&
        sameWorldShape(it->shape,shapes[i])) {
      const auto force = scene.m_Impl->physicsWorld->forces(it->body);
      f.fieldForce = force.fieldForce.vector(); f.contactForce = force.contactForce.vector(); f.totalForce = force.totalForce.vector(); f.solved = force.status != ForceStatus::Stale;
    }
    result.push_back(f);
  }
  return result;
}
} // namespace Jade::Physics
