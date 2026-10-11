#pragma once

#include <Jade/Application.hpp>

#include "../internal/Platform/Window.hpp"
#include "../internal/Events/EventDispatcher.hpp"

#include <optional>

namespace Jade {

struct Application::Impl {
  std::uint32_t frameLimits;
  bool logPerformance;
  bool debug;
  bool running = false;
  bool stopRequested = false;
  Detail::EventDispatcher events{"Application"};
  Window window;
  Runtime runtime;

  explicit Impl(const EngineConfig &config)
      : frameLimits(config.frameLimits), logPerformance(config.logPerformance), debug(config.debug.shortcuts),
        window(config.window), runtime(window, config)
  {
  }
};

} // namespace Jade
