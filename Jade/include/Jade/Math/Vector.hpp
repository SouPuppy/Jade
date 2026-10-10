#pragma once

namespace Jade {

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;

  constexpr Vec2() = default;
  constexpr Vec2(float xValue, float yValue) : x(xValue), y(yValue) {}

  constexpr float dot(Vec2 other) const noexcept { return x * other.x + y * other.y; }
  // In 2D, cross returns the signed area rather than a vector.
  constexpr float cross(Vec2 other) const noexcept { return x * other.y - y * other.x; }
};

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  constexpr Vec3() = default;
  constexpr Vec3(float xValue, float yValue, float zValue) : x(xValue), y(yValue), z(zValue) {}

  constexpr float dot(Vec3 other) const noexcept { return x * other.x + y * other.y + z * other.z; }
  constexpr Vec3 cross(Vec3 other) const noexcept
  {
    return {y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x};
  }
};

constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator-(Vec2 value) noexcept { return {-value.x, -value.y}; }
constexpr Vec2 operator*(Vec2 value, float scalar) noexcept { return {value.x * scalar, value.y * scalar}; }
constexpr Vec2 operator*(float scalar, Vec2 value) noexcept { return value * scalar; }
constexpr bool operator==(Vec2 a, Vec2 b) noexcept { return a.x == b.x && a.y == b.y; }
constexpr bool operator!=(Vec2 a, Vec2 b) noexcept { return !(a == b); }
constexpr float dot(Vec2 a, Vec2 b) noexcept { return a.dot(b); }
constexpr float cross(Vec2 a, Vec2 b) noexcept { return a.cross(b); }

constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator-(Vec3 value) noexcept { return {-value.x, -value.y, -value.z}; }
constexpr Vec3 operator*(Vec3 value, float scalar) noexcept { return {value.x * scalar, value.y * scalar, value.z * scalar}; }
constexpr Vec3 operator*(float scalar, Vec3 value) noexcept { return value * scalar; }
constexpr bool operator==(Vec3 a, Vec3 b) noexcept { return a.x == b.x && a.y == b.y && a.z == b.z; }
constexpr bool operator!=(Vec3 a, Vec3 b) noexcept { return !(a == b); }
constexpr float dot(Vec3 a, Vec3 b) noexcept { return a.dot(b); }
constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept { return a.cross(b); }

// Alternative dimensional spellings for the same vector value types.
using Vec2D = Vec2;
using Vec3D = Vec3;

} // namespace Jade
