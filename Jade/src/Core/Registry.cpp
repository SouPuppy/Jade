#include <Jade/ECS/Registry.hpp>
namespace Jade::ECS {
bool Registry::owns(Entity e) const noexcept { return e.m_Registry.lock() == m_Storage && e.valid(); }
Entity Registry::create()
{
  const auto [index, generation] = m_Storage->Create();
  return Entity(m_Storage, index, generation);
}

void Registry::destroy(Entity entity)
{
  if (!owns(entity)) throw std::invalid_argument("Foreign entity.");
  m_Storage->RequireMutation();
  if (m_DestroyObserver) m_DestroyObserver(entity);
  m_Storage->Destroy(entity.m_Index, entity.m_Generation);
}

std::vector<Entity> Registry::Snapshot(std::initializer_list<std::type_index> types) const
{
  std::vector<Entity> result;
  for (const auto [index, generation] : m_Storage->Matching(types)) {
    result.push_back(Entity(m_Storage, index, generation));
  }
  return result;
}

}
