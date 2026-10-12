#include "WorldImpl.hpp"
#include "../internal/Core/WorldTransforms.hpp"
#include <Jade/Physics/System.hpp>
#include <Jade/World/Streaming.hpp>
namespace Jade {
World::World() : m_Impl(std::make_unique<Impl>()) {
  m_Registry.m_DestroyObserver = [this](Entity e) { if (m_Streaming) m_Streaming->destroyed(e); };
}
World::~World() = default;
Physics::Environment& World::physics() noexcept { return m_Impl->physics; }
const Physics::Environment& World::physics() const noexcept { return m_Impl->physics; }
Physics::World& World::physicsWorld() {
  if (!m_Impl->physicsWorld) {
    m_Impl->physicsWorld = std::make_unique<Physics::World>();
    m_Impl->physicsWorld->setFields(m_Impl->physics.fields); m_Impl->physicsWorld->setIntegration(m_Impl->physics.integration);
    m_Impl->physicsFields = m_Impl->physics.fields; m_Impl->physicsIntegration = m_Impl->physics.integration;
  }
  return *m_Impl->physicsWorld;
}
Physics::BodyID World::physicsBody(Entity entity) const {
  if (!owns(entity)) throw std::invalid_argument("Foreign entity.");
  const auto& bindings = m_Impl->physicsBindings;
  const auto it = std::lower_bound(bindings.begin(),bindings.end(),entity.id(),[](const auto& b,auto id) { return b.entity.id() < id; });
  if (it == bindings.end() || it->entity != entity) return {};
  return it->body;
}

bool World::owns(Entity entity) const noexcept
{
  return entity.m_Registry.lock() == m_Registry.m_Storage && entity.valid();
}

void World::RequireOwned(Entity entity) const
{
  if (entity.m_Registry.lock() != m_Registry.m_Storage) {
    throw std::invalid_argument("Entity belongs to a different World.");
  }
  if (!entity.valid()) {
    throw std::logic_error("Entity is no longer alive.");
  }
}

void World::destroy(Entity entity)
{
  RequireOwned(entity);
  m_Registry.destroy(entity);
}

void World::setParent(Entity child, Entity parent)
{
  if (!owns(child) || (parent.id() != 0 && !owns(parent))) {
    throw std::invalid_argument("Parenting requires live entities in the same World.");
  }
  if (m_Streaming) m_Streaming->validateParent(child, parent);
  m_Registry.m_Storage->SetParent(child.m_Index, parent.id() == 0 ? std::nullopt : std::optional(parent.m_Index));
  if (parent && parent.has<Residency>()) {
    const auto residency = parent.get<Residency>();
    std::vector<Entity> nodes{child};
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      auto entity = nodes[i];
      if (!entity.has<Residency>()) entity.add<Residency>(residency);
      else if (entity.get<Residency>() != residency) { entity.get<Residency>() = residency; m_Registry.markChanged(entity); }
      auto descendants = children(entity); nodes.insert(nodes.end(),descendants.begin(),descendants.end());
    }
  }
}

Entity World::parent(Entity entity) const
{
  RequireOwned(entity);
  const auto parent = m_Registry.m_Storage->Parent(entity.m_Index);
  return parent ? Entity(m_Registry.m_Storage, parent->first, parent->second) : Entity{};
}

std::vector<Entity> World::children(Entity entity) const
{
  RequireOwned(entity);
  std::vector<Entity> result;
  for (const auto [index, generation] : m_Registry.m_Storage->Children(entity.m_Index)) {
    result.push_back(Entity(m_Registry.m_Storage, index, generation));
  }
  return result;
}

Matrix4 World::worldMatrix(Entity entity) const
{
  RequireOwned(entity);
  return Detail::WorldTransforms(*this).Resolve(entity).model;
}


void World::command(std::function<void(World&)> command) {
  if (!command) throw std::invalid_argument("World command cannot be empty.");
  m_Impl->commands.push_back(std::move(command));
}
void World::commit() {
  m_Registry.m_Storage->RequireMutation();
  auto commands = std::move(m_Impl->commands); m_Impl->commands.clear();
  for (auto& command : commands) command(*this);
}
StreamingManager& World::streaming() {
  if (!m_Streaming) m_Streaming = std::make_unique<StreamingManager>(*this);
  return *m_Streaming;
}
const StreamingManager& World::streaming() const { return const_cast<World*>(this)->streaming(); }
void World::updateStreaming(float dt) { commit(); if (m_Streaming) m_Streaming->update(dt); }
void World::save() { if (m_Streaming) { m_Streaming->flush(); m_Streaming->save(); m_Streaming->flush(); } }
void World::detachJobs() noexcept {
  if (m_Impl->physicsWorld) m_Impl->physicsWorld->setJobSystem(nullptr);
  if (m_Streaming) m_Streaming->detachJobs();
}
void World::tick(Physics::System& system, double dt, bool paused) {
  if (!std::isfinite(dt) || dt < 0) throw std::invalid_argument("Invalid world delta.");
  m_Impl->tickStats.substeps = 0;
  if (paused) { m_Impl->accumulator = 0; return; }
  const auto& config = system.config();
  m_Impl->accumulator += dt;
  commit();
  unsigned steps = 0;
  const bool fixedUpdate = static_cast<bool>(m_Impl->fixedUpdate);
  if (!fixedUpdate && m_Impl->accumulator + 1e-9 >= config.fixedStep) { commit(); system.synchronize(*this); }
  while (m_Impl->accumulator + 1e-9 >= config.fixedStep && steps < config.maxSubsteps) {
    if (fixedUpdate) {
      if (m_Impl->fixedUpdate) m_Impl->fixedUpdate(*this, static_cast<float>(config.fixedStep));
      commit(); system.synchronize(*this);
    }
    system.advance(*this, config.fixedStep); m_Impl->accumulator -= config.fixedStep; ++steps;
    if (fixedUpdate) system.publish(*this);
  }
  if (steps && !fixedUpdate) system.publish(*this);
  m_Impl->tickStats.substeps = steps; m_Impl->accumulator = std::max(0.0, m_Impl->accumulator);
  if (m_Impl->accumulator >= config.fixedStep) {
    const auto remainder = std::fmod(m_Impl->accumulator, config.fixedStep);
    m_Impl->tickStats.droppedTime += m_Impl->accumulator - remainder; m_Impl->accumulator = remainder;
  }
  if (m_Streaming) m_Streaming->migrate();
  commit();
}
void World::setFixedUpdate(std::function<void(World&, float)> update) { m_Impl->fixedUpdate = std::move(update); }
const WorldTickStatistics& World::tickStatistics() const noexcept { return m_Impl->tickStats; }
}
