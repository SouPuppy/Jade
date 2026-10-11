#pragma once

#include <Jade/Core/Entity.hpp>
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

namespace Physics { struct Environment; }

template<class... T>
class SceneView final {
public:
  class Iterator {
  public:
    using value_type = Entity;
    using reference = Entity;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    Iterator() = default;
    Entity operator*() const { return (*m_Entities)[m_Index]; }
    Iterator& operator++() { ++m_Index; Skip(); return *this; }
    Iterator operator++(int) { auto previous = *this; ++*this; return previous; }
    bool operator==(const Iterator&) const = default;

  private:
    friend class SceneView;
    Iterator(const std::vector<Entity>* entities, std::size_t index)
      : m_Entities(entities), m_Index(index) { Skip(); }
    void Skip()
    {
      while (m_Index < m_Entities->size()) {
        const auto& entity = (*m_Entities)[m_Index];
        if (entity.valid() && (entity.template has<T>() && ...)) break;
        ++m_Index;
      }
    }
    const std::vector<Entity>* m_Entities = nullptr;
    std::size_t m_Index = 0;
  };

  Iterator begin() const { return Iterator(&m_Entities, 0); }
  Iterator end() const { return Iterator(&m_Entities, m_Entities.size()); }

private:
  friend class Scene;
  explicit SceneView(std::vector<Entity> entities) : m_Entities(std::move(entities)) {}
  std::vector<Entity> m_Entities;
};

// Main-thread scene owner. New scenes are empty and do not require a window.
class Scene final {
public:
  Scene();
  ~Scene();
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;
  Physics::Environment& physics() noexcept;
  const Physics::Environment& physics() const noexcept;

  // Installs static void TModule::attach(Scene&) once per type and Scene.
  // Failed installations cancel new subscriptions and may be retried.
  template<class TModule>
  void use()
  {
    static_assert(std::is_same_v<decltype(TModule::attach(*this)), void>,
      "Scene modules provide static void attach(Jade::Scene&).");
    InstallModule(typeid(TModule), [this] { TModule::attach(*this); });
  }

  Entity create();
  // Create exactly the descriptor's components. No descriptor instance is constructed.
  template<class T> Entity create()
  {
    static_assert(Detail::ComponentsOf<T>::descriptor,
      "Scene::create<T> requires a Jade::Archetype or a type with using Components = Archetype<...>.");
    const auto entity = create();
    try {
      Detail::DefaultComponents(entity, typename Detail::ComponentsOf<T>::type{});
      return entity;
    } catch (...) {
      if (owns(entity)) destroy(entity);
      throw;
    }
  }
  void destroy(Entity entity);
  bool owns(Entity entity) const noexcept;
  // Snapshot identities; skip removals and defer new matches until the next view.
  template<class... T>
  auto query() const
  {
    static_assert(sizeof...(T) > 0, "Scene::query requires at least one component type.");
    return QueryTypes(typename Detail::FlattenComponents<Detail::ComponentList<>, T...>::type{});
  }

  // Reparenting retains local TRS. Empty parent detaches; deletion destroys the subtree.
  void setParent(Entity child, Entity parent = {});
  Entity parent(Entity entity) const;
  std::vector<Entity> children(Entity entity) const;
  Matrix4 worldMatrix(Entity entity) const;
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

private:
  friend class Application;
  friend class Runtime;
  template<class... T> SceneView<T...> QueryTypes(Detail::ComponentList<T...>) const {
    return SceneView<T...>(Snapshot({typeid(T)...}));
  }
  bool CapturesMouse() const noexcept;
  void InstallModule(std::type_index type, std::function<void()> attach);
  void RequireOwned(Entity entity) const;
  std::vector<Entity> Snapshot(std::initializer_list<std::type_index> types) const;
  EventConnection Connect(KeyAction, KeyChord, std::function<EventFlow(const KeyEvent&)>);
  EventConnection Connect(MouseAction, MouseButton, std::function<EventFlow(const MouseEvent&)>);
  EventConnection Connect(std::function<EventFlow(const FrameUpdate&)>);
  EventConnection Connect(std::function<EventFlow(const MouseMoved&)>);
  EventConnection Connect(std::function<EventFlow(const MouseScrolled&)>);
  EventConnection ConnectLifecycle(bool activated, std::function<EventFlow()>);
  struct Impl;
  std::shared_ptr<Detail::Registry> m_Registry;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade
