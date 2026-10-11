#pragma once
#include <Jade/WindowConfig.hpp>
#include <Jade/Renderer/System.hpp>
#include <Jade/Physics/System.hpp>
#include <Jade/Core/JobSystem.hpp>
#include <cstdint>

namespace Jade {
struct DebugConfig {
  bool shortcuts = false;
  bool axes = false;
  bool aabbs = false;
  bool physics = false;
};
struct EngineConfig {
  WindowConfig window;
  Renderer::Config renderer;
  Physics::Config physics;
  JobSystemConfig jobs;
  DebugConfig debug;
  std::uint32_t frameLimits = 24;
  bool logPerformance = true;
};
} // namespace Jade
