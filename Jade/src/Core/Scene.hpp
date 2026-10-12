#pragma once

#include <Jade/Core/Scene.hpp>
#include <Jade/Physics/Environment.hpp>
#include <Jade/Physics/World.hpp>
#include <Jade/Core/Transform.hpp>
#include <Jade/Physics/RigidBody.hpp>
#include "../internal/Physics/CollisionWorld.hpp"

#include "../internal/Events/EventDispatcher.hpp"
#include "../internal/Input/CameraNavigation.hpp"

namespace Jade {

struct Scene::Impl {
  struct ModuleEntry {
    std::type_index type;
    bool installed = false;
  };
  std::uint64_t id;
  Entity camera;
  const Runtime* owner = nullptr;
  std::vector<ModuleEntry> modules;
  Detail::EventDispatcher events;
  std::shared_ptr<Detail::CameraNavigation> cameraNavigation;
  Impl();
};

} // namespace Jade
