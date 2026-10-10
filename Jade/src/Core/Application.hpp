#pragma once

#include <Jade/Application.hpp>

#include "../internal/Platform/Window.hpp"
#include "../internal/Events/EventDispatcher.hpp"
#include "../internal/Renderer/Host/RenderHost.hpp"

#include <optional>

namespace Jade {

struct Application::Impl {
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
  bool wireframe = false;
  Detail::RenderHost rendering;

  explicit Impl(const ApplicationConfig &config)
      : frameLimits(config.frameLimits), logPerformance(config.logPerformance), debug(config.debug),
        axisGizmo(config.axisGizmo), window(config.window),
        rendering(window, config.vsync, config.window.title, config.rayTracing)
  {
  }
};

} // namespace Jade
