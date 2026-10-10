#pragma once

#include <Jade/Core/Entity.hpp>

#include <functional>
#include <initializer_list>
#include <memory>
#include <typeindex>
#include <vector>

namespace Jade {

// Main-thread component owner. No window, renderer or builtin component dependency.
class World final {
public:
  World();
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  Entity create();
  void destroy(Entity entity);
  bool owns(Entity entity) const noexcept;

  // Snapshot matching identities. New matches wait until the next query;
  // deleted entities/components are skipped. Removing a component invalidates its references.
  template<class... T, class Callback>
  void each(Callback&& callback)
  {
    static_assert(sizeof...(T) > 0, "World::each requires at least one component type.");
    for (const auto entity : Snapshot({typeid(T)...})) {
      if ((entity.has<T>() && ...)) {
        std::invoke(callback, entity, entity.get<T>()...);
      }
    }
  }

private:
  std::vector<Entity> Snapshot(std::initializer_list<std::type_index> types) const;
  std::shared_ptr<Detail::Registry> m_Registry;
};

} // namespace Jade
