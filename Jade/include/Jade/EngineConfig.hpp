#pragma once
#include <Jade/WindowConfig.hpp>
#include <Jade/Renderer/System.hpp>
#include <Jade/Physics/System.hpp>
#include <cstdint>

namespace Jade {
struct DebugConfig {
  bool shortcuts = false;
  bool axes = false;
  bool physics = false;
};
struct EngineConfig {
  WindowConfig window;
  Renderer::Config renderer;
  Physics::Config physics;
  DebugConfig debug;
  std::uint32_t frameLimits = 24;
  bool logPerformance = true;
};
} // namespace Jade
