#pragma once

#include <Jade/Core/World.hpp>
#include <Jade/Assets/Assets.hpp>
#include <Jade/Color.hpp>
#include <Jade/Events/Event.hpp>
#include <Jade/Events/FrameUpdate.hpp>
#include <Jade/Events/Keyboard.hpp>
#include <Jade/Events/Mouse.hpp>
#include <Jade/Events/MouseMoved.hpp>
#include <Jade/Events/MouseScrolled.hpp>
#include <Jade/WindowConfig.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace Jade {

struct ApplicationConfig {
  WindowConfig window;
  bool vsync = false;
  Color clearColor;
  // Frames per second. RunOptions::frameLimit controls total frames per run.
  std::uint32_t frameLimits = 24;
  // Debug summaries once per second, rate-limited hitch warnings and optional
  // per-frame Trace. Work time excludes the explicit frame-pacing wait.
  bool logPerformance = true;
  // Unit XYZ axes at the world origin, visible over objects. F3 toggles them.
  bool axisGizmo = true;
};

struct RunOptions {
  // Zero runs until the user closes the window.
  std::uint32_t frameLimit = 0;
  bool validateRendering = false;
};

class Application final {
public:
  explicit Application(const ApplicationConfig& config = {});
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  // World and Assets own data; entity/asset handles do not extend their lifetime.
  // Scene mutation and Run() belong to the main thread.
  World& world() noexcept;
  Assets& assets() noexcept;
  Entity camera() const noexcept;
  // Requires a live entity in this World with Transform and Camera.
  void setCamera(Entity camera);
  // Callbacks run on the main thread in registration order. Ignoring the
  // returned handle keeps the callback; use disconnect() to cancel it.
  template<class Event, class Callback>
  EventConnection on(Callback&& callback)
  {
    using Function = std::decay_t<Callback>;
    auto function = std::make_shared<Function>(std::forward<Callback>(callback));
    if constexpr (std::is_same_v<Event, FrameUpdate> || std::is_same_v<Event, MouseMoved>
        || std::is_same_v<Event, MouseScrolled>) {
      return Connect([function](const Event& event) {
        return InvokeCallback(*function, event);
      });
    } else if constexpr (std::is_base_of_v<MouseEvent, Event> && !std::is_same_v<MouseEvent, Event>) {
      return Connect(Event::action, Event::binding, [function](const MouseEvent& event) {
        return InvokeCallback(*function, Event(event));
      });
    } else {
      static_assert(std::is_base_of_v<KeyEvent, Event> && !std::is_same_v<KeyEvent, Event>,
        "Application::on supports KeyPressed/Released/Held, MousePressed/Released/Held, "
        "MouseMoved, MouseScrolled and FrameUpdate.");
      return Connect(Event::action, Event::chord, [function](const KeyEvent& event) {
        return InvokeCallback(*function, Event(event));
      });
    }
  }

  // Ends the current Run(), allowing a later Run() to resume.
  void stop() noexcept;
  // Applies to triangle meshes beginning with the next frame.
  void setWireframe(bool enabled) noexcept;
  void setAxisGizmo(bool enabled) noexcept;
  bool axisGizmoEnabled() const noexcept;
  void Run(const RunOptions& options = {});

private:
  template<class Callback, class Event>
  static EventFlow InvokeCallback(Callback& callback, const Event& event)
  {
    if constexpr (std::is_invocable_v<Callback&, const Event&>) {
      using Result = std::invoke_result_t<Callback&, const Event&>;
      static_assert(std::is_same_v<Result, void> || std::is_same_v<Result, EventFlow>,
        "Event callbacks return void or Jade::EventFlow.");
      if constexpr (std::is_same_v<Result, EventFlow>) {
        return std::invoke(callback, event);
      } else {
        std::invoke(callback, event);
        return EventFlow::Continue;
      }
    } else {
      static_assert(std::is_invocable_v<Callback&>,
        "Event callbacks accept either no arguments or const Event&.");
      using Result = std::invoke_result_t<Callback&>;
      static_assert(std::is_same_v<Result, void> || std::is_same_v<Result, EventFlow>,
        "Event callbacks return void or Jade::EventFlow.");
      if constexpr (std::is_same_v<Result, EventFlow>) {
        return std::invoke(callback);
      } else {
        std::invoke(callback);
        return EventFlow::Continue;
      }
    }
  }

  EventConnection Connect(KeyAction action, KeyChord chord,
    std::function<EventFlow(const KeyEvent&)> callback);
  EventConnection Connect(MouseAction action, MouseButton button,
    std::function<EventFlow(const MouseEvent&)> callback);
  EventConnection Connect(std::function<EventFlow(const FrameUpdate&)> callback);
  EventConnection Connect(std::function<EventFlow(const MouseMoved&)> callback);
  EventConnection Connect(std::function<EventFlow(const MouseScrolled&)> callback);
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade
