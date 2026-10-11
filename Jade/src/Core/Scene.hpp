#pragma once

#include <Jade/Core/Scene.hpp>

#include "../internal/Events/EventDispatcher.hpp"

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
  Impl();
};

} // namespace Jade
