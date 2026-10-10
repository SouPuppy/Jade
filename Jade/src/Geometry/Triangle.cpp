#include "Triangle.hpp"

#include "../internal/Math/Validation.hpp"

namespace Jade {

Triangle::Triangle(Vec2 a, Vec2 b, Vec2 c, Color color)
{
  setPos(a, b, c);
  setColor(color);
}

void Triangle::setPos(Vec2 a, Vec2 b, Vec2 c)
{
  Detail::Validate(a);
  Detail::Validate(b);
  Detail::Validate(c);
  m_Vertices = {a, b, c};
}

void Triangle::setColor(Color color)
{
  Detail::Validate(color);
  m_Color = color;
}

} // namespace Jade
