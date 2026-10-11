#pragma once

#include <Jade/Core/Scene.hpp>
#include <Jade/Assets/Assets.hpp>
#include <Jade/Color.hpp>
#include <Jade/Events/Event.hpp>
#include <Jade/Events/FrameUpdate.hpp>
#include <Jade/Events/Keyboard.hpp>
#include <Jade/Events/Mouse.hpp>
#include <Jade/Events/MouseMoved.hpp>
#include <Jade/Events/MouseScrolled.hpp>
#include <Jade/WindowConfig.hpp>
#include <Jade/Renderer/RayTracing/RayTracing.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace Jade {

struct ApplicationConfig {
  WindowConfig window;
  Renderer::RayTracingConfig rayTracing;
  bool vsync = false;
  // Built-in F/F3/F4/O shortcuts, independent of logging and build type.
  // Unmodified I toggles RayTracing/Wireframe even with debug disabled.
  bool debug = false;
  // Frames per second. RunOptions::frameLimit controls total frames per run.
  std::uint32_t frameLimits = 24;
  // Debug summaries once per second, rate-limited hitch warnings and optional
  // per-frame Trace. Work time excludes the explicit frame-pacing wait.
  bool logPerformance = true;
  // Unit XYZ axes at the world origin, visible over objects. Debug enables F3.
  bool axisGizmo = true;
  // Collision shapes (yellow); debug enables F4. Independent of Wireframe.
  bool colliderGizmo = false;
  // World AABBs (cyan), over either render mode.
  bool colliderAabbGizmo = false;
};

struct RunOptions {
  // Zero runs until the user closes the window.
  std::uint32_t frameLimit = 0;
};

class Application final {
public:
  explicit Application(const ApplicationConfig &config = {});
  ~Application();

  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;

  // Assets are shared across scenes. Selection is deferred while Run is active.
  Assets &assets() noexcept;
  // The owned path tracer is always available. Configure its integrator outside Run.
  Renderer::RayTracing &rayTracing() noexcept;
  Renderer::RenderProgress renderProgress() const;
  // Returns the committed selection, including nullptr when no scene is active.
  std::shared_ptr<Scene> scene() const noexcept;
  // Last request wins. nullptr unloads; selecting the current scene emits no events.
  void setScene(std::shared_ptr<Scene> scene);
  // Callbacks run on the main thread in registration order. Ignoring the
  // returned handle keeps the callback; use disconnect() to cancel it.
  template <class Event, class Callback> EventConnection on(Callback &&callback)
  {
    using Function = std::decay_t<Callback>;
    auto function = std::make_shared<Function>(std::forward<Callback>(callback));
    if constexpr (std::is_same_v<Event, FrameUpdate> || std::is_same_v<Event, MouseMoved> ||
                  std::is_same_v<Event, MouseScrolled>) {
      return Connect(
          [function](const Event &event) { return Detail::InvokeEventCallback(*function, event); });
    } else if constexpr (std::is_base_of_v<MouseEvent, Event> && !std::is_same_v<MouseEvent, Event>) {
      return Connect(Event::action, Event::binding, [function](const MouseEvent &event) {
        return Detail::InvokeEventCallback(*function, Event(event));
      });
    } else {
      static_assert(std::is_base_of_v<KeyEvent, Event> && !std::is_same_v<KeyEvent, Event>,
                    "Application::on supports KeyPressed/Released/Held, MousePressed/Released/Held, "
                    "MouseMoved, MouseScrolled and FrameUpdate.");
      return Connect(Event::action, Event::chord, [function](const KeyEvent &event) {
        return Detail::InvokeEventCallback(*function, Event(event));
      });
    }
  }

  // Ends the current Run(), allowing a later Run() to resume.
  void stop() noexcept;
  // Wireframe suspends path tracing and draws diagnostic edges.
  void setWireframe(bool enabled) noexcept;
  bool wireframeEnabled() const noexcept;
  void setAxisGizmo(bool enabled) noexcept;
  bool axisGizmoEnabled() const noexcept;
  void setColliderGizmo(bool enabled) noexcept;
  bool colliderGizmoEnabled() const noexcept;
  void setColliderAabbGizmo(bool enabled) noexcept;
  bool colliderAabbGizmoEnabled() const noexcept;
  void Run(const RunOptions &options = {});

private:
  EventConnection Connect(KeyAction action, KeyChord chord,
                          std::function<EventFlow(const KeyEvent &)> callback);
  EventConnection Connect(MouseAction action, MouseButton button,
                          std::function<EventFlow(const MouseEvent &)> callback);
  EventConnection Connect(std::function<EventFlow(const FrameUpdate &)> callback);
  EventConnection Connect(std::function<EventFlow(const MouseMoved &)> callback);
  EventConnection Connect(std::function<EventFlow(const MouseScrolled &)> callback);
  bool ApplyPendingScene();
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade
