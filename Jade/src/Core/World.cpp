#include "World.hpp"

#include "../internal/Core/Registry.hpp"

#include <stdexcept>

namespace Jade {

World::World() : m_Registry(std::make_shared<Detail::Registry>()) {}
World::~World() = default;

bool World::owns(Entity entity) const noexcept
{
  return entity.m_Registry.lock() == m_Registry && entity.valid();
}

Entity World::create()
{
  const auto [index, generation] = m_Registry->Create();
  return Entity(m_Registry, index, generation);
}

void World::destroy(Entity entity)
{
  if (entity.m_Registry.lock() != m_Registry) {
    throw std::invalid_argument("Entity belongs to a different World.");
  }
  m_Registry->Destroy(entity.m_Index, entity.m_Generation);
}

std::vector<Entity> World::Snapshot(std::initializer_list<std::type_index> types) const
{
  std::vector<Entity> result;
  for (const auto [index, generation] : m_Registry->Matching(types)) {
    result.push_back(Entity(m_Registry, index, generation));
  }
  return result;
}

} // namespace Jade
