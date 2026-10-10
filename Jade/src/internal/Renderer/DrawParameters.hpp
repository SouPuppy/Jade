#pragma once

#include <Jade/Color.hpp>

#include "../Math/Matrix4.hpp"

namespace Jade::Detail {

// Renderer input contains no Entity, World, Camera or component ownership.
struct DrawParameters {
  Matrix4 transform = Matrix4::Identity();
  Matrix4 normalTransform = Matrix4::Identity();
  Color color{1, 1, 1, 1};
  bool depthTest = true;
  bool depthWrite = true;
};

} // namespace Jade::Detail
