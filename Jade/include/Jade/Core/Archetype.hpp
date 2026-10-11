#pragma once

#include <Jade/Core/Entity.hpp>
#include <stdexcept>
#include <type_traits>

namespace Jade::Detail {
template<class... T> struct ComponentList {};
template<class T, class = void> struct ComponentsOf { using type = ComponentList<T>; };
template<class T> struct ComponentsOf<T, std::void_t<typename T::JadeArchetypeComponents>> {
  using type = typename T::JadeArchetypeComponents;
};
template<class List, class T> struct AppendUnique;
template<class... Ts, class T> struct AppendUnique<ComponentList<Ts...>, T> {
  using type = std::conditional_t<(std::is_same_v<T, Ts> || ...), ComponentList<Ts...>, ComponentList<Ts..., T>>;
};
template<class Result, class Input> struct MergeComponents;
template<class Result> struct MergeComponents<Result, ComponentList<>> { using type = Result; };
template<class Result, class T, class... Rest>
struct MergeComponents<Result, ComponentList<T, Rest...>> {
  using type = typename MergeComponents<typename AppendUnique<Result, T>::type, ComponentList<Rest...>>::type;
};
template<class Result, class... T> struct FlattenComponents;
template<class Result> struct FlattenComponents<Result> { using type = Result; };
template<class Result, class T, class... Rest> struct FlattenComponents<Result, T, Rest...> {
  using type = typename FlattenComponents<typename MergeComponents<Result, typename ComponentsOf<T>::type>::type, Rest...>::type;
};
template<class List> class ArchetypeHandle;
template<class... T> class ArchetypeHandle<ComponentList<T...>> : public Entity {
public:
  using JadeArchetypeComponents = ComponentList<T...>;
  explicit ArchetypeHandle(Entity entity) : Entity(entity) {
    if (!valid()) throw std::logic_error("Archetypes require a live Scene entity.");
    (Default<T>(), ...);
  }
private:
  template<class Component> void Default() {
    if constexpr (std::is_default_constructible_v<Component>) add<Component>();
  }
};
template<class... T> void CheckArchetype(Entity entity, ComponentList<T...>) {
  if (!(entity.has<T>() && ...))
    throw std::logic_error("Archetype constructor did not initialize all declared components.");
}
} // namespace Jade::Detail

namespace Jade {
// Composition is flattened and deduplicated. Only the final handle's constructor runs.
template<class... T>
using Archetype = Detail::ArchetypeHandle<typename Detail::FlattenComponents<Detail::ComponentList<>, T...>::type>;
} // namespace Jade
