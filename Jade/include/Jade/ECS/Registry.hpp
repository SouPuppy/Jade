#pragma once
#include <Jade/Core/Entity.hpp>
#include <Jade/Core/Archetype.hpp>
#include <Jade/Math/Matrix4.hpp>
#include <iterator>
namespace Jade { class World; }
namespace Jade::ECS {
template<class... T>
class View final {
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
    friend class View;
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
  friend class Registry;
  explicit View(std::vector<Entity> entities) : m_Entities(std::move(entities)) {}
  std::vector<Entity> m_Entities;
};


class Registry final {
public:
  Registry() : m_Storage(std::make_shared<Detail::Registry>()) {}
  Registry(const Registry&) = delete;
  Registry& operator=(const Registry&) = delete;
  Entity create();
  // Create exactly the descriptor's components. No descriptor instance is constructed.
  template<class T> Entity create()
  {
    static_assert(Detail::ComponentsOf<T>::descriptor,
      "Registry::create<T> requires a Jade::Archetype or a type with using Components = Archetype<...>.");
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
  void markChanged(Entity entity) {
    if (!owns(entity)) throw std::invalid_argument("Expected a live entity in this Registry.");
    m_Storage->Changed(entity.m_Index);
  }
  std::uint64_t revision(Entity entity) const {
    if (!owns(entity)) throw std::invalid_argument("Expected a live entity in this Registry.");
    return m_Storage->Revision(entity.m_Index);
  }
  template<class T, class F> void patch(Entity entity, F&& fn) {
    if (!owns(entity)) throw std::invalid_argument("Expected a live entity in this Registry.");
    try { fn(entity.get<T>()); }
    catch (...) { markChanged(entity); throw; }
    markChanged(entity);
  }
  // Snapshot identities; skip removals and defer new matches until the next view.
  template<class... T>
  auto query() const
  {
    static_assert(sizeof...(T) > 0, "Registry::query requires at least one component type.");
    return QueryTypes(typename Detail::FlattenComponents<Detail::ComponentList<>, T...>::type{});
  }


  template<class... T, class F> void each(F&& fn) const {
    m_Storage->Visit({typeid(T)...}, [&](auto index, auto generation) {
      Entity e(m_Storage, index, generation); fn(e, m_Storage->Get<T>(index)...);
    });
  }
private:
  friend class Jade::World;
  template<class... T> View<T...> QueryTypes(Detail::ComponentList<T...>) const {
    return View<T...>(Snapshot({typeid(T)...}));
  }
  std::vector<Entity> Snapshot(std::initializer_list<std::type_index> types) const;
  std::shared_ptr<Detail::Registry> m_Storage;
  std::function<void(Entity)> m_DestroyObserver;
};
} // namespace Jade::ECS
