#pragma once
#include <Jade/World/World.hpp>
#include <Jade/Physics/Environment.hpp>
#include <Jade/Physics/World.hpp>
#include <Jade/Physics/System.hpp>
#include <Jade/Core/Transform.hpp>
#include "../internal/Physics/CollisionWorld.hpp"
namespace Jade {
struct World::Impl {
  Physics::Environment physics;
  std::unique_ptr<Physics::World> physicsWorld;
  struct PhysicsBinding {
    Entity entity;
    Physics::BodyID body;
    Transform published;
    RigidBody authored;
    Detail::WorldCollider shape;
    bool rigid = false;
  };
  std::vector<PhysicsBinding> physicsBindings;
  std::vector<std::shared_ptr<const Physics::ForceField>> physicsFields;
  Physics::Integration physicsIntegration = Physics::Integration::SemiImplicitEuler;
  std::vector<Physics::Manifold> physicsContacts;
  std::vector<std::shared_ptr<const Physics::ForceField>> publishedFields;
  Physics::Integration publishedIntegration = Physics::Integration::SemiImplicitEuler;
  std::uint64_t physicsPublisher = 0, physicsEpoch = 0;

  double accumulator = 0;
  WorldTickStatistics tickStats;
  std::function<void(World&, float)> fixedUpdate;
  const Runtime* owner = nullptr;
  std::vector<std::function<void(World&)>> commands;
};
}
