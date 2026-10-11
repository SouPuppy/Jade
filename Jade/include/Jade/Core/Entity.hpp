#pragma once

#include <cstdint>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <utility>

namespace Jade {
namespace Detail { class Registry; }
class Scene;

// A copyable identity. Components belong to the Scene, not to this handle.
// A const handle can access mutable components, like a const pointer value.
class Entity {
public:
  Entity() = default;

  template<class T, class... Args>
  T& add(Args&&... args) const
  {
    static_assert(std::is_same_v<T, std::remove_cvref_t<T>>, "Components must be unqualified object types.");
    CheckAdd(typeid(T));
    auto component = std::make_shared<T>(std::forward<Args>(args)...);
    return *static_cast<T*>(Add(typeid(T), std::move(component)));
  }

  template<class T> T& get() const { return *static_cast<T*>(Get(typeid(T))); }
  template<class T> bool has() const noexcept { return Has(typeid(T)); }
  template<class T> void remove() const { Remove(typeid(T)); }

  bool valid() const noexcept;
  explicit operator bool() const noexcept { return valid(); }
  std::uint64_t id() const noexcept { return (std::uint64_t{m_Generation} << 32) | m_Index; }

  friend bool operator==(const Entity& a, const Entity& b) noexcept
  {
    return a.id() == b.id() && !a.m_Registry.owner_before(b.m_Registry)
      && !b.m_Registry.owner_before(a.m_Registry);
  }

private:
  friend class Scene;
  Entity(const std::shared_ptr<Detail::Registry>& registry, std::uint32_t index, std::uint32_t generation)
    : m_Registry(registry), m_Index(index), m_Generation(generation) {}

  void CheckAdd(std::type_index type) const;
  void* Add(std::type_index type, std::shared_ptr<void> component) const;
  void* Get(std::type_index type) const;
  bool Has(std::type_index type) const noexcept;
  void Remove(std::type_index type) const;
  std::shared_ptr<Detail::Registry> Lock() const;

  std::weak_ptr<Detail::Registry> m_Registry;
  std::uint32_t m_Index = 0;
  std::uint32_t m_Generation = 0;
};

} // namespace Jade
