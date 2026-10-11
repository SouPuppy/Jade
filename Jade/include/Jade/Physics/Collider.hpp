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
// Empty colliders do not participate in physics. Assign Collider{} to clear a shape,
// or Collider{Box{...}} / Collider{Sphere{...}} to bind one.
class Collider final {
public:
  constexpr Collider() noexcept : m_Kind(Kind::None), m_Shape() {}
  explicit constexpr Collider(Box box) noexcept : m_Kind(Kind::Box), m_Shape(box) {}
  explicit constexpr Collider(Sphere sphere) noexcept : m_Kind(Kind::Sphere), m_Shape(sphere) {}
  constexpr bool empty() const noexcept { return m_Kind == Kind::None; }

private:
  friend struct Detail::ColliderAccess;
  enum class Kind : std::uint8_t { None, Box, Sphere };
  union Shape {
    std::uint8_t none;
    Box box;
    Sphere sphere;
    constexpr Shape() noexcept : none(0) {}
    constexpr Shape(Box value) noexcept : box(value) {}
    constexpr Shape(Sphere value) noexcept : sphere(value) {}
  };
  Kind m_Kind;
  Shape m_Shape;
};

} // namespace Jade
