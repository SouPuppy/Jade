#include "Registry.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Jade::Detail {

std::pair<std::uint32_t, std::uint32_t> Registry::Create()
{
  m_Groups[0].entities.reserve(m_Groups[0].entities.size() + 1);
  if (!m_Free.empty()) {
    const auto index = m_Free.back();
    m_Free.pop_back();
    m_Slots[index].alive = true;
    m_Slots[index].group = 0;
    m_Slots[index].row = m_Groups[0].entities.size();
    m_Groups[0].entities.push_back(index);
    return {index, m_Slots[index].generation};
  }
  if (m_Slots.size() >= std::numeric_limits<std::uint32_t>::max()) {
    throw std::overflow_error("Scene has exhausted its entity slots.");
  }
  const auto index = static_cast<std::uint32_t>(m_Slots.size());
  m_Slots.push_back({});
  m_Slots[index].row = m_Groups[0].entities.size();
  m_Groups[0].entities.push_back(index);
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
  // Collect without recursion or mutation. All allocation precedes invalidation.
  std::vector<std::uint32_t> nodes{index};
  for (std::size_t cursor = 0; cursor < nodes.size(); ++cursor) {
    const auto& children = m_Slots[nodes[cursor]].children;
    nodes.insert(nodes.end(), children.begin(), children.end());
  }
  std::reverse(nodes.begin(), nodes.end());
  std::size_t count = 0;
  for (const auto node : nodes) {
    for (const auto& [type, pool] : m_Pools) {
      (void)type;
      count += pool.contains(node);
    }
  }
  std::vector<std::shared_ptr<void>> removed;
  removed.reserve(count);
  m_Free.reserve(m_Free.size() + nodes.size());
  constexpr auto none = std::numeric_limits<std::uint32_t>::max();
  const auto parent = m_Slots[index].parent;
  if (parent != none) std::erase(m_Slots[parent].children, index);
  for (const auto node : nodes) {
    RemoveFromGroup(node);
    auto& slot = m_Slots[node];
    slot.alive = false;
    slot.parent = none;
    slot.children.clear();
    if (slot.generation != none) {
      ++slot.generation;
      m_Free.push_back(node);
    }
  }
  for (const auto node : nodes) {
    for (auto& [type, pool] : m_Pools) {
      (void)type;
      const auto found = pool.find(node);
      if (found != pool.end()) {
        removed.push_back(std::move(found->second));
        pool.erase(found);
      }
    }
  }
  // No live container iterator or doomed identity crosses a user destructor.
  for (auto& component : removed) component.reset();
}

bool Registry::Has(std::uint32_t index, std::type_index type) const noexcept
{
  const auto pool = m_Pools.find(type);
  return pool != m_Pools.end() && pool->second.contains(index);
}

void* Registry::Add(std::uint32_t index, std::type_index type, std::shared_ptr<void> component)
{
  auto signature = m_Groups[m_Slots[index].group].types;
  if (std::binary_search(signature.begin(), signature.end(), type))
    throw std::logic_error("Entity already has this component.");
  signature.insert(std::lower_bound(signature.begin(), signature.end(), type), type);
  const auto group = FindGroup(std::move(signature));
  m_Groups[group].entities.reserve(m_Groups[group].entities.size() + 1);
  const auto [entry, inserted] = m_Pools[type].emplace(index, std::move(component));
  if (!inserted) {
    throw std::logic_error("Entity already has this component.");
  }
  ChangeGroup(index, group);
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
    if (!pool->second.contains(index)) return;
    auto signature = m_Groups[m_Slots[index].group].types;
    std::erase(signature, type);
    const auto group = FindGroup(std::move(signature));
    m_Groups[group].entities.reserve(m_Groups[group].entities.size() + 1);
    ChangeGroup(index, group);
    // Node extraction ensures a destructor cannot invalidate a live map operation.
    auto removed = pool->second.extract(index);
  }
}

void Registry::SetParent(std::uint32_t child, std::optional<std::uint32_t> parent)
{
  constexpr auto none = std::numeric_limits<std::uint32_t>::max();
  const auto next = parent.value_or(none);
  if (m_Slots[child].parent == next) return;
  for (auto ancestor = next; ancestor != none; ancestor = m_Slots[ancestor].parent) {
    if (ancestor == child) throw std::invalid_argument("Parenting would create a cycle.");
  }
  if (next != none) m_Slots[next].children.reserve(m_Slots[next].children.size() + 1);
  const auto previous = m_Slots[child].parent;
  if (previous != none) std::erase(m_Slots[previous].children, child);
  m_Slots[child].parent = next;
  if (next != none) m_Slots[next].children.push_back(child);
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> Registry::Parent(std::uint32_t index) const
{
  const auto parent = m_Slots[index].parent;
  if (parent == std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
  return std::pair{parent, m_Slots[parent].generation};
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> Registry::Children(std::uint32_t index) const
{
  std::vector<std::pair<std::uint32_t, std::uint32_t>> result;
  for (const auto child : m_Slots[index].children) result.emplace_back(child, m_Slots[child].generation);
  return result;
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> Registry::Matching(
  std::initializer_list<std::type_index> types) const
{
  std::vector<std::pair<std::uint32_t, std::uint32_t>> result;
  std::vector<std::type_index> signature(types);
  std::sort(signature.begin(), signature.end());
  signature.erase(std::unique(signature.begin(), signature.end()), signature.end());
  auto found = m_QueryGroups.find(signature);
  if (found == m_QueryGroups.end()) {
    std::vector<std::size_t> groups;
    for (std::size_t i = 0; i < m_Groups.size(); ++i)
      if (std::includes(m_Groups[i].types.begin(), m_Groups[i].types.end(), signature.begin(), signature.end()))
        groups.push_back(i);
    found = m_QueryGroups.emplace(std::move(signature), std::move(groups)).first;
  }
  m_LastQueryRows = 0;
  for (const auto group : found->second)
    for (const auto index : m_Groups[group].entities) {
      ++m_LastQueryRows;
      result.emplace_back(index, m_Slots[index].generation);
    }
  std::sort(result.begin(), result.end());
  return result;
}

std::size_t Registry::FindGroup(std::vector<std::type_index> types)
{
  if (const auto found = m_GroupLookup.find(types); found != m_GroupLookup.end()) return found->second;
  const auto index = m_Groups.size();
  m_Groups.push_back({types, {}});
  try { m_GroupLookup.emplace(std::move(types), index); }
  catch (...) { m_Groups.pop_back(); throw; }
  m_QueryGroups.clear();
  return index;
}
void Registry::RemoveFromGroup(std::uint32_t index) noexcept
{
  auto& slot = m_Slots[index];
  auto& members = m_Groups[slot.group].entities;
  const auto last = members.back();
  members[slot.row] = last;
  m_Slots[last].row = slot.row;
  members.pop_back();
}
void Registry::ChangeGroup(std::uint32_t index, std::size_t group) noexcept
{
  RemoveFromGroup(index);
  auto& slot = m_Slots[index];
  slot.group = group;
  slot.row = m_Groups[group].entities.size();
  m_Groups[group].entities.push_back(index);
}

} // namespace Jade::Detail
