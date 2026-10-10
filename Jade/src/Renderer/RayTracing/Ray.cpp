#include "Ray.hpp"
#include "../Common/Math.hpp"
namespace Jade::Renderer {
Ray SurfaceHit::spawn(Vec3 direction) const
{
  direction = Math::unit(direction);
  const float offset = dot(Math::abs(geometricNormal), positionError);
  Vec3 displacement = geometricNormal * (dot(direction, geometricNormal) < 0 ? -offset : offset);
  Vec3 origin = position + displacement;
  float *coordinates[3] = {&origin.x, &origin.y, &origin.z};
  const Vec3 side = dot(direction, geometricNormal) < 0 ? -geometricNormal : geometricNormal;
  const float offsets[3] = {side.x, side.y, side.z};
  for (int axis = 0; axis < 3; ++axis)
    if (offsets[axis] != 0)
      *coordinates[axis] =
          std::nextafter(*coordinates[axis], offsets[axis] > 0 ? std::numeric_limits<float>::infinity()
                                                               : -std::numeric_limits<float>::infinity());
  return {origin, direction};
}

} // namespace Jade::Renderer
