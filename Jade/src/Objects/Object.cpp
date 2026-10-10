#include "Object.hpp"

#include "../internal/Math/Validation.hpp"
#include "../internal/Mesh/MeshValidation.hpp"

#include <utility>

namespace Jade {

void Object::setPos(Vec3 position)
{
  Detail::Validate(position);
  m_Position = position;
}

void Object::setRotation(Vec3 degrees)
{
  Detail::Validate(degrees);
  m_Rotation = degrees;
}

void Object::setScale(Vec3 scale)
{
  Detail::ValidatePositive(scale.x);
  Detail::ValidatePositive(scale.y);
  Detail::ValidatePositive(scale.z);
  m_Scale = scale;
}

void Object::setMesh(Mesh mesh)
{
  Detail::ValidateMesh(mesh);
  m_Mesh = std::move(mesh);
}

void Object::setColor(Color color)
{
  Detail::Validate(color);
  m_Color = color;
}

} // namespace Jade
