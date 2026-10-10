#pragma once

#include <Jade/Application.hpp>

#include "../internal/Platform/Window.hpp"
#include "../internal/Events/EventDispatcher.hpp"
#include "../internal/Renderer/Renderer.hpp"

#include <optional>

namespace Jade {

struct Application::Impl {
  Color clearColor;
  std::uint32_t frameLimits;
  bool logPerformance;
  bool debug;
  bool axisGizmo;
  bool running = false;
  bool stopRequested = false;
  bool switching = false;
  Detail::EventDispatcher events{"Application"};
  Assets assets;
  std::shared_ptr<Scene> scene;
  std::optional<std::shared_ptr<Scene>> pendingScene;
  Window window;
  Renderer renderer;

  explicit Impl(const ApplicationConfig& config)
    : clearColor(config.clearColor),
      frameLimits(config.frameLimits),
      logPerformance(config.logPerformance),
      debug(config.debug),
      axisGizmo(config.axisGizmo),
      window(config.window),
      renderer(window, config.vsync)
  {}
};

} // namespace Jade
