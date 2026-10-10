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
#include <Jade/Renderer/Common/Backend.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <typeindex>

namespace Jade {

struct ApplicationConfig {
  WindowConfig window;
  bool vsync = false;
  // Built-in unmodified F/F3/O shortcuts. I cycles registered renderers independently. Independent of logging and build type.
  bool debug = false;
  // Frames per second. RunOptions::frameLimit controls total frames per run.
  std::uint32_t frameLimits = 24;
  // Debug summaries once per second, rate-limited hitch warnings and optional
  // per-frame Trace. Work time excludes the explicit frame-pacing wait.
  bool logPerformance = true;
  // Unit XYZ axes at the world origin, visible over objects. Debug enables F3.
  bool axisGizmo = true;
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
  // Register one owned backend per concrete type. Configure outside Run.
  template <class TRenderer, class... Args> TRenderer &use(Args &&...args)
  {
    static_assert(std::is_base_of_v<Renderer::Backend, TRenderer>,
                  "Application::use requires a Renderer::Backend.");
    auto backend = std::make_unique<TRenderer>(std::forward<Args>(args)...);
    auto &result = *backend;
    InstallRenderer(typeid(TRenderer), std::move(backend));
    return result;
  }
  template<class TRenderer> void setRenderer() { SelectRenderer(typeid(TRenderer)); }
  const Renderer::Backend* activeRenderer() const noexcept;
  void cycleRenderer();
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
  // Wireframe bypasses the computational backend and draws diagnostic edges.
  void setWireframe(bool enabled) noexcept;
  bool wireframeEnabled() const noexcept;
  void setAxisGizmo(bool enabled) noexcept;
  bool axisGizmoEnabled() const noexcept;
  void Run(const RunOptions &options = {});

private:
  void InstallRenderer(std::type_index type, std::unique_ptr<Renderer::Backend> backend);
  void SelectRenderer(std::type_index type);
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
