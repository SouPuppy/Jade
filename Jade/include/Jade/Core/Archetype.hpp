#pragma once

#include <Jade/Core/Entity.hpp>
#include <stdexcept>
#include <type_traits>

namespace Jade::Detail {
template<class... T> struct ComponentList {};
template<class List> struct ArchetypeDescriptor {};
template<class T, class = void> struct ComponentsOf {
  using type = ComponentList<T>;
  static constexpr bool descriptor = false;
};
template<class List> struct ComponentsOf<ArchetypeDescriptor<List>> {
  using type = List;
  static constexpr bool descriptor = true;
};
// Components is the public convention for a named, type-only descriptor.
template<class T> struct ComponentsOf<T, std::void_t<typename T::Components>> {
  static_assert(ComponentsOf<typename T::Components>::descriptor,
    "Descriptor::Components must name a Jade::Archetype or another descriptor.");
  using type = typename ComponentsOf<typename T::Components>::type;
  static constexpr bool descriptor = true;
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
template<class... T> void DefaultComponents(Entity entity, ComponentList<T...>) {
  static_assert((std::is_default_constructible_v<T> && ...),
    "Archetype components must be default-constructible; configure resources after creation.");
  (entity.add<T>(), ...);
  if (!(entity.has<T>() && ...))
    throw std::logic_error("Component construction removed declared components.");
}
} // namespace Jade::Detail

namespace Jade {
// Type-only composition, flattened and deduplicated in first-appearance order.
// Named descriptors provide using Components = Archetype<...>.
template<class... T>
using Archetype = Detail::ArchetypeDescriptor<typename Detail::FlattenComponents<Detail::ComponentList<>, T...>::type>;
} // namespace Jade
