#include "Entity.hpp"

#include "../internal/Core/Registry.hpp"

#include <stdexcept>

namespace Jade {

std::shared_ptr<Detail::Registry> Entity::Lock() const
{
  auto registry = m_Registry.lock();
  if (!registry || !registry->Alive(m_Index, m_Generation)) {
    throw std::logic_error("Entity is no longer alive.");
  }
  return registry;
}

bool Entity::valid() const noexcept
{
  const auto registry = m_Registry.lock();
  return registry && registry->Alive(m_Index, m_Generation);
}

void Entity::Remove(std::type_index type) const
{
  Lock()->Remove(m_Index, type);
}

} // namespace Jade
