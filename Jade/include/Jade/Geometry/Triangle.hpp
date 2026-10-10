#pragma once

#include <Jade/Color.hpp>
#include <Jade/Vector.hpp>

#include <array>

namespace Jade {

// Filled 2D triangle using the same drawing units as Line.
class Triangle final {
public:
  Triangle() = default;
  Triangle(Vec2 a, Vec2 b, Vec2 c, Color color = {1, 1, 1, 1});

  void setPos(Vec2 a, Vec2 b, Vec2 c);
  void setColor(Color color);
  const std::array<Vec2, 3>& Vertices() const noexcept { return m_Vertices; }
  const Color& GetColor() const noexcept { return m_Color; }

private:
  std::array<Vec2, 3> m_Vertices{};
  Color m_Color{1, 1, 1, 1};
};

} // namespace Jade
