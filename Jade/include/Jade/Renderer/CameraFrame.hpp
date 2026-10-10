#pragma once

#include <Jade/Graphics/Camera.hpp>
#include <Jade/Math/Matrix4.hpp>
#include <Jade/Renderer/Image.hpp>
#include <Jade/Renderer/Ray.hpp>

namespace Jade::Renderer {

struct CameraFrame {
  Camera lens;
  Vec3 position;
  Vec3 right{1, 0, 0};
  Vec3 up{0, 1, 0};
  Vec3 forward{0, 0, -1};
  Ray generateRay(Vec2 pixelSample, ImageSize size) const;
  Matrix4 viewProjection(ImageSize size) const;
  bool operator==(const CameraFrame &) const = default;
};

} // namespace Jade::Renderer
