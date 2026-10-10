#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

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
  std::vector<std::pair<std::uint32_t, std::uint32_t>> Matching(
    std::initializer_list<std::type_index> types) const;

private:
  struct Slot {
    std::uint32_t generation = 1;
    bool alive = true;
  };
  using Pool = std::unordered_map<std::uint32_t, std::shared_ptr<void>>;
  std::vector<Slot> m_Slots;
  std::vector<std::uint32_t> m_Free;
  std::unordered_map<std::type_index, Pool> m_Pools;
};

} // namespace Jade::Detail
