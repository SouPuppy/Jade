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

void Entity::CheckAdd(std::type_index type) const
{
  if (Lock()->Has(m_Index, type)) {
    throw std::logic_error("Entity already has this component.");
  }
}

void* Entity::Add(std::type_index type, std::shared_ptr<void> component) const
{
  return Lock()->Add(m_Index, type, std::move(component));
}

void* Entity::Get(std::type_index type) const
{
  return Lock()->Get(m_Index, type);
}

bool Entity::Has(std::type_index type) const noexcept
{
  const auto registry = m_Registry.lock();
  return registry && registry->Alive(m_Index, m_Generation) && registry->Has(m_Index, type);
}

void Entity::Remove(std::type_index type) const
{
  Lock()->Remove(m_Index, type);
}

} // namespace Jade
