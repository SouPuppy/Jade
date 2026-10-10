#include "Line.hpp"

#include "../internal/Math/Validation.hpp"

namespace Jade {

Line::Line(Vec2D start, Vec2D end)
{
  setPos(start, end);
}

void Line::setPos(Vec2D start, Vec2D end)
{
  Detail::Validate(start);
  Detail::Validate(end);
  m_Start = start;
  m_End = end;
}

} // namespace Jade
