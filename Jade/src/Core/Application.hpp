#pragma once

#include <Jade/Application.hpp>

#include "../internal/Platform/Window.hpp"
#include "../internal/Events/EventDispatcher.hpp"
#include "../internal/Renderer/Renderer.hpp"

#include <vector>

namespace Jade {

struct Application::Impl {
  Color clearColor;
  std::uint32_t frameLimits;
  bool logFrames;
  bool axisGizmo;
  bool running = false;
  bool stopRequested = false;
  Detail::EventDispatcher events;
  Window window;
  Renderer renderer;
  std::vector<std::unique_ptr<Line>> lines;
  std::vector<std::unique_ptr<Triangle>> triangles;
  std::vector<std::unique_ptr<Object>> objects;
  std::vector<std::unique_ptr<Camera>> cameras;
  Camera defaultCamera;
  Camera* activeCamera = &defaultCamera;

  explicit Impl(const ApplicationConfig& config)
    : clearColor(config.clearColor),
      frameLimits(config.frameLimits),
      logFrames(config.logFrames),
      axisGizmo(config.axisGizmo),
      window(config.window),
      renderer(window, config.vsync, config.viewHeight)
  {
  }
};

} // namespace Jade
