#pragma once

namespace Jade {

enum class Projection { Perspective, Orthographic };

// Projection data only. Position/orientation live in the entity's Transform.
struct Camera {
  Projection projection = Projection::Perspective;
  float fovDegrees = 60;
  float viewHeight = 6;
  float nearPlane = 0.1f;
  float farPlane = 100;
  bool operator==(const Camera&) const = default;
};

} // namespace Jade
