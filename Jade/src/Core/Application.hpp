#pragma once

#include <Jade/Application.hpp>

#include "../internal/Platform/Window.hpp"
#include "../internal/Events/EventDispatcher.hpp"
#include "../internal/Renderer/Renderer.hpp"

#include <Jade/Core/Transform.hpp>
#include <Jade/Graphics/Camera.hpp>
#include <Jade/Graphics/Motion.hpp>

namespace Jade {

struct Application::Impl {
  Color clearColor;
  std::uint32_t frameLimits;
  bool logFrames;
  bool axisGizmo;
  bool running = false;
  bool stopRequested = false;
  Detail::EventDispatcher events;
  Assets assets;
  World world;
  Window window;
  Renderer renderer;
  Entity activeCamera;

  explicit Impl(const ApplicationConfig& config)
    : clearColor(config.clearColor),
      frameLimits(config.frameLimits),
      logFrames(config.logFrames),
      axisGizmo(config.axisGizmo),
      window(config.window),
      renderer(window, config.vsync)
  {
    activeCamera = world.create();
    auto& pose = activeCamera.add<Transform>();
    pose.position = {3, 3, 5};
    Motion::lookAt(pose, {0, 0, 0});
    activeCamera.add<Camera>();
  }
};

} // namespace Jade
