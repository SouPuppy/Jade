#pragma once
#include <Jade/ECS/Registry.hpp>
#include <Jade/Physics/Types.hpp>
#include <functional>
namespace Jade {
class JobSystem; class Assets;
namespace Physics { struct Environment; class World; class System; }
class StreamingManager;
class Runtime;
struct WorldTickStatistics { unsigned substeps = 0; double droppedTime = 0; };
class World final {
public:
  World();
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  ECS::Registry& registry() noexcept { return m_Registry; }
  const ECS::Registry& registry() const noexcept { return m_Registry; }
  Entity create() { return m_Registry.create(); }
  template<class T> Entity create() { return m_Registry.create<T>(); }
  void destroy(Entity);
  bool owns(Entity) const noexcept;
  void setParent(Entity child, Entity parent = {});
  Entity parent(Entity) const;
  std::vector<Entity> children(Entity) const;
  Matrix4 worldMatrix(Entity) const;
  Physics::Environment& physics() noexcept;
  const Physics::Environment& physics() const noexcept;
  Physics::World& physicsWorld();
  Physics::BodyID physicsBody(Entity) const;
  void command(std::function<void(World&)>);
  void commit();
  StreamingManager& streaming();
  const StreamingManager& streaming() const;
  void updateStreaming(float dt);
  void save();
  void tick(Physics::System&, double dt, bool paused = false);
  void setFixedUpdate(std::function<void(World&, float)>);
  const WorldTickStatistics& tickStatistics() const noexcept;
  void detachJobs() noexcept;
private:
  friend class Physics::System;
  friend class Runtime;
  friend class StreamingManager;
  void RequireOwned(Entity) const;
  ECS::Registry m_Registry;
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
  std::unique_ptr<StreamingManager> m_Streaming;
};
}
