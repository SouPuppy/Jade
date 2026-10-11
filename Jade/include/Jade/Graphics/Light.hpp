#pragma once
#include <Jade/Renderer/Common/LinearRGB.hpp>
#include <Jade/Math/Vector.hpp>

namespace Jade {
enum class LightType { Directional, Point, Area };
// Linear radiance; point intensity is radiant intensity, area intensity radiance.
// Directional intensity is incident irradiance. Local forward is -Z.
struct Light {
  LightType type = LightType::Point;
  Renderer::LinearRGB intensity{1,1,1};
  Vec2 size{1,1};
  bool twoSided = false;
  bool operator==(const Light&) const = default;
};
}
