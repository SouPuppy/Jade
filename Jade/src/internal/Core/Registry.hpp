#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <limits>
#include <optional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <map>

namespace Jade::Detail {

class Registry final {
public:
  std::pair<std::uint32_t, std::uint32_t> Create();
  void Destroy(std::uint32_t index, std::uint32_t generation);
  bool Alive(std::uint32_t index, std::uint32_t generation) const noexcept;
  bool Has(std::uint32_t index, std::type_index type) const noexcept;
  void* Add(std::uint32_t index, std::type_index type, std::shared_ptr<void> component);
  void* Get(std::uint32_t index, std::type_index type) const;
  void Remove(std::uint32_t index, std::type_index type);
  void SetParent(std::uint32_t child, std::optional<std::uint32_t> parent);
  std::optional<std::pair<std::uint32_t, std::uint32_t>> Parent(std::uint32_t index) const;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> Children(std::uint32_t index) const;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> Matching(
    std::initializer_list<std::type_index> types) const;
  std::size_t LastQueryRows() const noexcept { return m_LastQueryRows; }

private:
  struct Slot {
    std::uint32_t generation = 1;
    bool alive = true;
    std::uint32_t parent = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> children;
    std::size_t group = 0, row = 0;
  };
  struct Group {
    std::vector<std::type_index> types;
    std::vector<std::uint32_t> entities;
  };
  std::size_t FindGroup(std::vector<std::type_index> types);
  void ChangeGroup(std::uint32_t index, std::size_t group) noexcept;
  void RemoveFromGroup(std::uint32_t index) noexcept;
  using Pool = std::unordered_map<std::uint32_t, std::shared_ptr<void>>;
  std::vector<Slot> m_Slots;
  std::vector<std::uint32_t> m_Free;
  std::unordered_map<std::type_index, Pool> m_Pools;
  std::vector<Group> m_Groups{Group{}};
  std::map<std::vector<std::type_index>, std::size_t> m_GroupLookup{{{}, 0}};
  mutable std::map<std::vector<std::type_index>, std::vector<std::size_t>> m_QueryGroups;
  mutable std::size_t m_LastQueryRows = 0;
};

} // namespace Jade::Detail
