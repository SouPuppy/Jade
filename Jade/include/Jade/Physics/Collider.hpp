#pragma once

#include <Jade/Math/Vector.hpp>

#include <cstdint>

namespace Jade {

// Local shapes, centered at the Entity's origin. Dimensions must be finite and
// positive when consumed. Transform supplies position, orientation and scale.
struct Box {
  Vec3 halfExtents{0.5f, 0.5f, 0.5f};
};

struct Sphere {
  float radius = 0.5f;
};

namespace Detail { struct ColliderAccess; }

// Scene-owned component. Shape data has value semantics and no separate allocation.
// Replace a shape by assigning Collider{Box{...}} or Collider{Sphere{...}}.
class Collider final {
public:
  Collider() = delete;
  explicit constexpr Collider(Box box) noexcept : m_Kind(Kind::Box), m_Shape(box) {}
  explicit constexpr Collider(Sphere sphere) noexcept : m_Kind(Kind::Sphere), m_Shape(sphere) {}

private:
  friend struct Detail::ColliderAccess;
  enum class Kind : std::uint8_t { Box, Sphere };
  union Shape {
    Box box;
    Sphere sphere;
    constexpr Shape(Box value) noexcept : box(value) {}
    constexpr Shape(Sphere value) noexcept : sphere(value) {}
  };
  Kind m_Kind;
  Shape m_Shape;
};

} // namespace Jade
