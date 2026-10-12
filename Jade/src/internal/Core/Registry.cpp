#include "Registry.hpp"
namespace Jade::Detail {
std::pair<std::uint32_t, std::uint32_t> Registry::Create()
{
  RequireMutation();
  if (m_Groups[0].entities.size() == m_Groups[0].entities.capacity())
    m_Groups[0].entities.reserve(std::max<std::size_t>(16, m_Groups[0].entities.size() * 2));
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
    throw std::overflow_error("Registry has exhausted its entity slots.");
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


void Registry::Destroy(std::uint32_t index, std::uint32_t generation) {
  RequireMutation();
  if (!Alive(index, generation)) throw std::logic_error("Dead entity.");
  std::vector<std::uint32_t> nodes{index};
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const auto& children = m_Slots[nodes[i]].children;
    nodes.insert(nodes.end(), children.begin(), children.end());
  }
  std::reverse(nodes.begin(), nodes.end());
  std::vector<std::function<void()>> retired;
  std::size_t total = 0;
  for (auto node : nodes) total += m_Groups[m_Slots[node].group].types.size();
  retired.reserve(total);
  if (m_Free.size() + nodes.size() > m_Free.capacity())
    m_Free.reserve(std::max(m_Free.size() + nodes.size(), std::max<std::size_t>(16, m_Free.capacity() * 2)));
  // Prepare every allocation before changing any live identity.
  for (auto node : nodes) {
    const auto& types = m_Groups[m_Slots[node].group].types;
    for (auto type : types) retired.push_back(m_Pools[m_TypeIds.at(type)]->prepare(node));
  }
  for (auto node : nodes) {
    for (auto type : m_Groups[m_Slots[node].group].types) m_Pools[m_TypeIds.at(type)]->unlink(node);
  }
  auto parent = m_Slots[index].parent;
  if (parent != UINT32_MAX) std::erase(m_Slots[parent].children, index);
  for (auto node : nodes) {
    RemoveFromGroup(node);
    auto& slot = m_Slots[node]; slot.alive = false; slot.parent = UINT32_MAX; slot.children.clear();
    if (slot.generation != UINT32_MAX) { ++slot.generation; m_Free.push_back(node); }
  }
  for (auto& destroy : retired) if (destroy) destroy();
}
bool Registry::Has(std::uint32_t e, std::type_index type) const noexcept {
  auto it = m_TypeIds.find(type);
  return it != m_TypeIds.end() && m_Pools[it->second]->has(e);
}
void* Registry::Get(std::uint32_t e, std::type_index type) const {
  auto it = m_TypeIds.find(type);
  if (it == m_TypeIds.end()) throw std::out_of_range("Missing component.");
  return m_Pools[it->second]->get(e);
}
void Registry::AddSignature(std::uint32_t e, std::type_index type) {
  const auto previous = m_Slots[e].group;
  const auto cached = m_Groups[previous].additions.find(type);
  std::size_t group;
  if (cached != m_Groups[previous].additions.end()) group = cached->second;
  else {
    auto types = m_Groups[previous].types;
    if (std::binary_search(types.begin(), types.end(), type)) throw std::logic_error("Duplicate component.");
    types.insert(std::lower_bound(types.begin(), types.end(), type), type);
    group = FindGroup(std::move(types));
    m_Groups[previous].additions.emplace(type,group);
  }
  if (m_Groups[group].entities.size() == m_Groups[group].entities.capacity())
    m_Groups[group].entities.reserve(std::max<std::size_t>(16, m_Groups[group].entities.size() * 2));
  ChangeGroup(e, group);
  Changed(e);
}
void Registry::Remove(std::uint32_t e, std::type_index type) {
  RequireMutation();
  if (!Has(e, type)) return;
  const auto previous = m_Slots[e].group;
  const auto cached = m_Groups[previous].removals.find(type);
  std::size_t group;
  if (cached != m_Groups[previous].removals.end()) group = cached->second;
  else {
    auto types = m_Groups[previous].types; std::erase(types, type);
    group = FindGroup(std::move(types));
    m_Groups[previous].removals.emplace(type,group);
  }
  if (m_Groups[group].entities.size() == m_Groups[group].entities.capacity())
    m_Groups[group].entities.reserve(std::max<std::size_t>(16, m_Groups[group].entities.size() * 2));
  auto retired = m_Pools[m_TypeIds.at(type)]->detach(e);
  ChangeGroup(e, group); Changed(e); if (retired) retired();
}
void Registry::SetParent(std::uint32_t child, std::optional<std::uint32_t> parent)
{
  RequireMutation();
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
  Changed(child);
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


const std::vector<std::size_t>& Registry::Groups(std::initializer_list<std::type_index> types) const {
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
  return found->second;
}
std::vector<std::pair<std::uint32_t, std::uint32_t>> Registry::Matching(std::initializer_list<std::type_index> types) const {
  std::vector<std::pair<std::uint32_t, std::uint32_t>> result;
  m_LastQueryRows = 0;
  for (auto group : Groups(types)) for (auto index : m_Groups[group].entities) {
    ++m_LastQueryRows; result.emplace_back(index, m_Slots[index].generation);
  }
  std::sort(result.begin(), result.end()); return result;
}
void Registry::Visit(std::initializer_list<std::type_index> types, const std::function<void(std::uint32_t, std::uint32_t)>& fn) {
  const auto& groups = Groups(types); ++m_IterationDepth;
  struct Guard { unsigned& depth; ~Guard() { --depth; } } guard{m_IterationDepth};
  for (auto group : groups) for (auto index : m_Groups[group].entities) fn(index, m_Slots[index].generation);
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
