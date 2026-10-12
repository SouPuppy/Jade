#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Jade::Detail {
inline std::atomic<std::size_t> nextComponentType{0};
template<class T> std::size_t ComponentType() {
  static const auto id = nextComponentType.fetch_add(1);
  return id;
}
struct ComponentPool {
  virtual ~ComponentPool() = default;
  virtual bool has(std::uint32_t) const noexcept = 0;
  virtual void* get(std::uint32_t) const = 0;
  virtual std::function<void()> prepare(std::uint32_t) = 0;
  virtual void unlink(std::uint32_t) noexcept = 0;
  std::function<void()> detach(std::uint32_t e) { auto action=prepare(e); unlink(e); return action; }
};
// Slots never move. Detached objects are destroyed after identity invalidation.
template<class T> class TypedPool final : public ComponentPool,
                                        public std::enable_shared_from_this<TypedPool<T>> {
  static constexpr std::uint32_t none = UINT32_MAX;
  static constexpr std::uint32_t building = none - 1;
  static constexpr std::size_t pageSize = 256;
  struct Cell { alignas(T) std::byte bytes[sizeof(T)]; bool live = false; std::uint32_t next = none; };
  using Page = std::array<Cell, pageSize>;
  std::vector<std::unique_ptr<Page>> pages;
  std::vector<std::uint32_t> sparse;
  std::uint32_t freeHead = none;
  std::uint32_t count = 0;
  Cell& cell(std::uint32_t i) { return (*pages[i / pageSize])[i % pageSize]; }
  T* object(std::uint32_t i) { return std::launder(reinterpret_cast<T*>(cell(i).bytes)); }
public:
  ~TypedPool() override {
    for (std::uint32_t i = 0; i < count; ++i) if (cell(i).live) std::destroy_at(object(i));
  }
  template<class... Args> T& emplace(std::uint32_t entity, Args&&... args) {
    if (entity < sparse.size() && sparse[entity] != none) throw std::logic_error("Duplicate or recursive component construction.");
    if (sparse.size() <= entity) sparse.resize(static_cast<std::size_t>(entity) + 1, none);
    const auto slot = freeHead == none ? count : freeHead;
    if (slot / pageSize == pages.size()) pages.push_back(std::make_unique<Page>());
    if (freeHead == none) ++count; else freeHead=cell(slot).next;
    sparse[entity] = building;
    try { std::construct_at(reinterpret_cast<T*>(cell(slot).bytes), std::forward<Args>(args)...); }
    catch (...) { sparse[entity]=none;cell(slot).next=freeHead;freeHead=slot;throw; }
    cell(slot).live = true;
    sparse[entity] = slot;
    return *object(slot);
  }
  bool has(std::uint32_t e) const noexcept override { return e < sparse.size() && sparse[e] < building; }
  void* get(std::uint32_t e) const override {
    if (!has(e)) throw std::out_of_range("Missing component.");
    return const_cast<TypedPool*>(this)->object(sparse[e]);
  }
  std::function<void()> prepare(std::uint32_t e) override {
    if (!has(e)) return {};
    const auto slot = sparse[e];
    auto owner = this->shared_from_this();
    std::function<void()> retire = [owner, slot] {
      owner->cell(slot).live = false;
      std::destroy_at(owner->object(slot));
      owner->cell(slot).next=owner->freeHead;owner->freeHead=slot;
    };
    return retire;
  }
  void unlink(std::uint32_t e) noexcept override { if(e<sparse.size())sparse[e]=none; }
};
class Registry final {
public:
  std::pair<std::uint32_t, std::uint32_t> Create();
  void Destroy(std::uint32_t, std::uint32_t);
  bool Alive(std::uint32_t, std::uint32_t) const noexcept;
  bool Has(std::uint32_t, std::type_index) const noexcept;
  template<class T, class... Args> T& Add(std::uint32_t e, Args&&... args) {
    RequireMutation();
    const auto generation = m_Slots.at(e).generation;
    const auto id = ComponentType<T>();
    if (m_Pools.size() <= id) m_Pools.resize(id + 1);
    if (!m_Pools[id]) {
      m_Pools[id] = std::make_shared<TypedPool<T>>();
      m_TypeIds.emplace(typeid(T), id);
    }
    auto pool = std::static_pointer_cast<TypedPool<T>>(m_Pools[id]);
    auto& result = pool->emplace(e, std::forward<Args>(args)...);
    try {
      if (!Alive(e, generation)) throw std::logic_error("Component constructor destroyed its owner.");
      AddSignature(e, typeid(T));
    }
    catch (...) { auto retire = pool->detach(e); retire(); throw; }
    return result;
  }
  template<class T> T& Get(std::uint32_t e) const {
    const auto id = ComponentType<T>();
    if (id >= m_Pools.size() || !m_Pools[id]) throw std::out_of_range("Missing component.");
    return *static_cast<T*>(m_Pools[id]->get(e));
  }
  template<class T> bool Has(std::uint32_t e) const noexcept {
    const auto id = ComponentType<T>();
    return id < m_Pools.size() && m_Pools[id] && m_Pools[id]->has(e);
  }
  void* Get(std::uint32_t, std::type_index) const;
  void Remove(std::uint32_t, std::type_index);
  void SetParent(std::uint32_t, std::optional<std::uint32_t>);
  std::optional<std::pair<std::uint32_t, std::uint32_t>> Parent(std::uint32_t) const;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> Children(std::uint32_t) const;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> Matching(std::initializer_list<std::type_index>) const;
  void Visit(std::initializer_list<std::type_index>, const std::function<void(std::uint32_t, std::uint32_t)>&);
  std::size_t LastQueryRows() const noexcept { return m_LastQueryRows; }
  void Changed(std::uint32_t e) { ++m_Slots.at(e).revision; }
  std::uint64_t Revision(std::uint32_t e) const { return m_Slots.at(e).revision; }
  void RequireMutation() const { if (m_IterationDepth) throw std::logic_error("Structural edit during system traversal; use commands."); }
private:
  struct Slot {
    std::uint32_t generation = 1;
    bool alive = true;
    std::uint64_t revision = 0;
    std::uint32_t parent = UINT32_MAX;
    std::vector<std::uint32_t> children;
    std::size_t group = 0, row = 0;
  };
  struct Group {
    std::vector<std::type_index> types;
    std::vector<std::uint32_t> entities;
    std::unordered_map<std::type_index,std::size_t> additions, removals;
  };
  std::size_t FindGroup(std::vector<std::type_index>);
  void AddSignature(std::uint32_t, std::type_index);
  void ChangeGroup(std::uint32_t, std::size_t) noexcept;
  void RemoveFromGroup(std::uint32_t) noexcept;
  const std::vector<std::size_t>& Groups(std::initializer_list<std::type_index>) const;
  std::vector<Slot> m_Slots;
  std::vector<std::uint32_t> m_Free;
  std::vector<std::shared_ptr<ComponentPool>> m_Pools;
  std::unordered_map<std::type_index, std::size_t> m_TypeIds;
  std::vector<Group> m_Groups{Group{}};
  std::map<std::vector<std::type_index>, std::size_t> m_GroupLookup{{{}, 0}};
  mutable std::map<std::vector<std::type_index>, std::vector<std::size_t>> m_QueryGroups;
  mutable std::size_t m_LastQueryRows = 0;
  unsigned m_IterationDepth = 0;
};
} // namespace Jade::Detail
