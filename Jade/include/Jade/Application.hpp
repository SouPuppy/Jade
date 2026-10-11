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
#include <Jade/Runtime.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace Jade {

struct RunOptions {
  // Zero runs until the user closes the window.
  std::uint32_t frameLimit = 0;
};

class Application final {
public:
  explicit Application(const EngineConfig &config = {});
  ~Application();

  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;

  Runtime &runtime() noexcept;
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
  void Run(const RunOptions &options = {});

private:
  EventConnection Connect(KeyAction action, KeyChord chord,
                          std::function<EventFlow(const KeyEvent &)> callback);
  EventConnection Connect(MouseAction action, MouseButton button,
                          std::function<EventFlow(const MouseEvent &)> callback);
  EventConnection Connect(std::function<EventFlow(const FrameUpdate &)> callback);
  EventConnection Connect(std::function<EventFlow(const MouseMoved &)> callback);
  EventConnection Connect(std::function<EventFlow(const MouseScrolled &)> callback);
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade
