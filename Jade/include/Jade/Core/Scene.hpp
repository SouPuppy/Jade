#pragma once

#include <Jade/World/World.hpp>
#include <Jade/Core/Archetype.hpp>
#include <Jade/Events/Event.hpp>
#include <Jade/Events/FrameUpdate.hpp>
#include <Jade/Events/Keyboard.hpp>
#include <Jade/Events/Mouse.hpp>
#include <Jade/Events/MouseMoved.hpp>
#include <Jade/Events/MouseScrolled.hpp>
#include <Jade/Events/SceneLifecycle.hpp>
#include <Jade/Math/Matrix4.hpp>

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

namespace Jade {

namespace Module { struct FPSCameraControl; }

// Owner-thread observation and input context for an explicitly supplied World.
class Scene final {
public:
  explicit Scene(std::shared_ptr<World> world);
  World& world() const noexcept { return *m_World; }
  std::shared_ptr<World> worldHandle() const noexcept { return m_World; }
  ~Scene();
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;

  // Installs static void TModule::attach(Scene&) once per type and Scene.
  // Failed installations cancel new subscriptions and may be retried.
  template<class TModule>
  void use()
  {
    static_assert(std::is_same_v<decltype(TModule::attach(*this)), void>,
      "Scene modules provide static void attach(Jade::Scene&).");
    InstallModule(typeid(TModule), [this] { TModule::attach(*this); });
  }

  Entity camera() const noexcept;
  void setCamera(Entity camera);

  // Callbacks run on the main thread in registration order. Ignoring the
  // returned handle keeps the callback; use disconnect() to cancel it.
  template<class Event, class Callback>
  EventConnection on(Callback&& callback)
  {
    using Function = std::decay_t<Callback>;
    auto function = std::make_shared<Function>(std::forward<Callback>(callback));
    if constexpr (std::is_same_v<Event, SceneActivated> || std::is_same_v<Event, SceneDeactivated>) {
      return ConnectLifecycle(std::is_same_v<Event, SceneActivated>, [function] {
        return Detail::InvokeEventCallback(*function, Event{});
      });
    } else if constexpr (std::is_same_v<Event, FrameUpdate> || std::is_same_v<Event, MouseMoved>
        || std::is_same_v<Event, MouseScrolled>) {
      return Connect([function](const Event& event) {
        return Detail::InvokeEventCallback(*function, event);
      });
    } else if constexpr (std::is_base_of_v<MouseEvent, Event> && !std::is_same_v<MouseEvent, Event>) {
      return Connect(Event::action, Event::binding, [function](const MouseEvent& event) {
        return Detail::InvokeEventCallback(*function, Event(event));
      });
    } else {
      static_assert(std::is_base_of_v<KeyEvent, Event> && !std::is_same_v<KeyEvent, Event>,
        "Scene::on supports KeyPressed/Released/Held, MousePressed/Released/Held, "
        "MouseMoved, MouseScrolled, FrameUpdate, SceneActivated and SceneDeactivated.");
      return Connect(Event::action, Event::chord, [function](const KeyEvent& event) {
        return Detail::InvokeEventCallback(*function, Event(event));
      });
    }
  }

  // Observe an action across all physical keys/buttons, regardless of modifiers.
  // Typed chord subscriptions above still match modifiers exactly.
  template<class Callback>
  EventConnection on(KeyAction action, Callback&& callback)
  {
    using Function = std::decay_t<Callback>;
    auto function = std::make_shared<Function>(std::forward<Callback>(callback));
    return Connect(action, {}, [function](const KeyEvent& event) {
      return Detail::InvokeEventCallback(*function, event);
    });
  }
  template<class Callback>
  EventConnection on(MouseAction action, Callback&& callback)
  {
    using Function = std::decay_t<Callback>;
    auto function = std::make_shared<Function>(std::forward<Callback>(callback));
    return Connect(action, MouseButton::Unknown, [function](const MouseEvent& event) {
      return Detail::InvokeEventCallback(*function, event);
    });
  }

private:
  friend class Application;
  friend class Runtime;
  friend struct Module::FPSCameraControl;
  bool CapturesMouse() const noexcept;
  void InstallModule(std::type_index type, std::function<void()> attach);
  EventConnection Connect(KeyAction, KeyChord, std::function<EventFlow(const KeyEvent&)>);
  EventConnection Connect(MouseAction, MouseButton, std::function<EventFlow(const MouseEvent&)>);
  EventConnection Connect(std::function<EventFlow(const FrameUpdate&)>);
  EventConnection Connect(std::function<EventFlow(const MouseMoved&)>);
  EventConnection Connect(std::function<EventFlow(const MouseScrolled&)>);
  EventConnection ConnectLifecycle(bool activated, std::function<EventFlow()>);
  struct Impl;
  std::shared_ptr<World> m_World;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade
