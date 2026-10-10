#include "Registry.hpp"

#include <limits>
#include <stdexcept>

namespace Jade::Detail {

std::pair<std::uint32_t, std::uint32_t> Registry::Create()
{
  if (!m_Free.empty()) {
    const auto index = m_Free.back();
    m_Free.pop_back();
    m_Slots[index].alive = true;
    return {index, m_Slots[index].generation};
  }
  if (m_Slots.size() >= std::numeric_limits<std::uint32_t>::max()) {
    throw std::overflow_error("World has exhausted its entity slots.");
  }
  const auto index = static_cast<std::uint32_t>(m_Slots.size());
  m_Slots.push_back({});
  return {index, 1};
}

bool Registry::Alive(std::uint32_t index, std::uint32_t generation) const noexcept
{
  return index < m_Slots.size() && m_Slots[index].alive && m_Slots[index].generation == generation;
}

void Registry::Destroy(std::uint32_t index, std::uint32_t generation)
{
  if (!Alive(index, generation)) {
    throw std::logic_error("Entity is no longer alive.");
  }
  // Extract before running user destructors, which may reenter the World.
  std::vector<std::shared_ptr<void>> removed;
  removed.reserve(m_Pools.size());
  if (generation != std::numeric_limits<std::uint32_t>::max()) {
    m_Free.reserve(m_Free.size() + 1);
  }
  m_Slots[index].alive = false;
  for (auto& [type, pool] : m_Pools) {
    (void)type;
    const auto found = pool.find(index);
    if (found != pool.end()) {
      removed.push_back(std::move(found->second));
      pool.erase(found);
    }
  }
  if (generation != std::numeric_limits<std::uint32_t>::max()) {
    ++m_Slots[index].generation;
    m_Free.push_back(index);
  }
}

bool Registry::Has(std::uint32_t index, std::type_index type) const noexcept
{
  const auto pool = m_Pools.find(type);
  return pool != m_Pools.end() && pool->second.contains(index);
}

void* Registry::Add(std::uint32_t index, std::type_index type, std::shared_ptr<void> component)
{
  const auto [entry, inserted] = m_Pools[type].emplace(index, std::move(component));
  if (!inserted) {
    throw std::logic_error("Entity already has this component.");
  }
  return entry->second.get();
}

void* Registry::Get(std::uint32_t index, std::type_index type) const
{
  const auto pool = m_Pools.find(type);
  if (pool == m_Pools.end()) {
    throw std::out_of_range("Entity does not have this component.");
  }
  const auto entry = pool->second.find(index);
  if (entry == pool->second.end()) {
    throw std::out_of_range("Entity does not have this component.");
  }
  return entry->second.get();
}

void Registry::Remove(std::uint32_t index, std::type_index type)
{
  const auto pool = m_Pools.find(type);
  if (pool != m_Pools.end()) {
    // Node extraction ensures a destructor cannot invalidate a live map operation.
    auto removed = pool->second.extract(index);
  }
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> Registry::Matching(
  std::initializer_list<std::type_index> types) const
{
  std::vector<std::pair<std::uint32_t, std::uint32_t>> result;
  for (std::size_t index = 0; index < m_Slots.size(); ++index) {
    if (!m_Slots[index].alive) {
      continue;
    }
    bool matches = true;
    for (const auto type : types) {
      matches = matches && Has(static_cast<std::uint32_t>(index), type);
    }
    if (matches) {
      result.emplace_back(static_cast<std::uint32_t>(index), m_Slots[index].generation);
    }
  }
  return result;
}

} // namespace Jade::Detail
