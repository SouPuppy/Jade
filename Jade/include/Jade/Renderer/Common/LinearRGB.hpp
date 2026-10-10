#pragma once

#include <algorithm>
#include <cmath>

namespace Jade::Renderer {

// Linear RGB radiance/reflectance, independent of display Color and alpha.
struct LinearRGB {
  float r = 0;
  float g = 0;
  float b = 0;
  bool operator==(const LinearRGB &) const = default;
  float maximum() const noexcept { return std::max({r, g, b}); }
  float luminance() const noexcept { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }
  bool finite() const noexcept { return std::isfinite(r) && std::isfinite(g) && std::isfinite(b); }
};

constexpr LinearRGB operator+(LinearRGB a, LinearRGB b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
constexpr LinearRGB operator*(LinearRGB a, LinearRGB b) { return {a.r * b.r, a.g * b.g, a.b * b.b}; }
constexpr LinearRGB operator*(LinearRGB a, float b) { return {a.r * b, a.g * b, a.b * b}; }
constexpr LinearRGB operator*(float a, LinearRGB b) { return b * a; }
constexpr LinearRGB operator/(LinearRGB a, float b) { return a * (1 / b); }
inline LinearRGB &operator+=(LinearRGB &a, LinearRGB b) { return a = a + b; }
inline LinearRGB &operator*=(LinearRGB &a, LinearRGB b) { return a = a * b; }
inline LinearRGB &operator*=(LinearRGB &a, float b) { return a = a * b; }

} // namespace Jade::Renderer
