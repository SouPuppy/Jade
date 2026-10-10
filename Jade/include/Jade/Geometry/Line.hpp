#pragma once

#include <Jade/Vector.hpp>

namespace Jade {

// A single line segment between two endpoints in window drawing units.
class Line final {
public:
  Line() = default;
  Line(Vec2D start, Vec2D end);

  void setPos(Vec2D start, Vec2D end);
  const Vec2D& Start() const noexcept { return m_Start; }
  const Vec2D& End() const noexcept { return m_End; }

private:
  Vec2D m_Start;
  Vec2D m_End;
};

} // namespace Jade
