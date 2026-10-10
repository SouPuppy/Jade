#include "CameraFrame.hpp"
#include "Math.hpp"
#include "../../internal/Math/Matrix4.hpp"
namespace Jade::Renderer {
Ray CameraFrame::generateRay(Vec2 sample, ImageSize size) const
{
  (void)size.pixels();
  Jade::Detail::ValidateCamera(lens);
  Jade::Detail::Validate(sample);
  Jade::Detail::Validate(position);
  const float aspect = static_cast<float>(size.width) / size.height;
  const float halfHeight = lens.projection == Projection::Perspective
                               ? std::tan(lens.fovDegrees * Math::Pi / 360)
                               : lens.viewHeight / 2;
  const float x = (2 * sample.x / size.width - 1) * halfHeight * aspect;
  const float y = (1 - 2 * sample.y / size.height) * halfHeight;
  Ray ray;
  if (lens.projection == Projection::Perspective) {
    ray.origin = position;
    ray.direction = Math::unit(forward + right * x + up * y);
    const float cosine = dot(ray.direction, Math::unit(forward));
    ray.tMin = lens.nearPlane / cosine;
    ray.tMax = lens.farPlane / cosine;
  } else {
    ray.origin = position + right * x + up * y;
    ray.direction = Math::unit(forward);
    ray.tMin = lens.nearPlane;
    ray.tMax = lens.farPlane;
  }
  return ray;
}

Matrix4 CameraFrame::viewProjection(ImageSize size) const
{
  (void)size.pixels();
  Matrix4 view = Matrix4::Identity();
  const Vec3 rows[3] = {right, up, -forward};
  for (std::size_t row = 0; row < 3; ++row) {
    view.at(row, 0) = rows[row].x;
    view.at(row, 1) = rows[row].y;
    view.at(row, 2) = rows[row].z;
    view.at(row, 3) = -dot(rows[row], position);
  }
  return Jade::Detail::ProjectionMatrix(lens, static_cast<double>(size.width) / size.height) * view;
}

} // namespace Jade::Renderer
