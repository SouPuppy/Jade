#include "Camera.hpp"

#include "../internal/Math/Matrix4.hpp"

namespace Jade {
namespace {

Vec3 CameraAxis(const Camera& camera, std::size_t column, double sign)
{
  const auto eye = camera.Position();
  const auto target = camera.Target();
  const auto up = camera.Up();
  const auto forward = Detail::Normalize({static_cast<double>(target.x) - eye.x,
    static_cast<double>(target.y) - eye.y, static_cast<double>(target.z) - eye.z});
  const auto right = Detail::Normalize(Detail::Cross(forward, {up.x, up.y, up.z}));
  const auto vertical = Detail::Cross(right, forward);
  const auto rotation = Detail::CameraRotationMatrix(camera.Rotation());
  const auto direction = Detail::Normalize({
    sign * (right[0] * rotation.at(0, column) + vertical[0] * rotation.at(1, column)
      - forward[0] * rotation.at(2, column)),
    sign * (right[1] * rotation.at(0, column) + vertical[1] * rotation.at(1, column)
      - forward[1] * rotation.at(2, column)),
    sign * (right[2] * rotation.at(0, column) + vertical[2] * rotation.at(1, column)
      - forward[2] * rotation.at(2, column))
  });
  return {static_cast<float>(direction[0]), static_cast<float>(direction[1]),
    static_cast<float>(direction[2])};
}

} // namespace

Camera::Camera(Projection projection) : m_Projection(projection)
{
  setProjection(projection);
  Object::setPos({3, 3, 5});
}

void Camera::setProjection(Projection projection)
{
  if (projection != Projection::Perspective && projection != Projection::Orthographic) {
    throw std::invalid_argument("Unknown camera projection mode.");
  }
  m_Projection = projection;
}

void Camera::setPos(Vec3 position)
{
  const auto previous = Position();
  const Vec3 target{
    static_cast<float>(static_cast<double>(m_Target.x) + position.x - previous.x),
    static_cast<float>(static_cast<double>(m_Target.y) + position.y - previous.y),
    static_cast<float>(static_cast<double>(m_Target.z) + position.z - previous.z)
  };
  Detail::ValidateCameraFrame(position, target, m_Up);
  Object::setPos(position);
  m_Target = target;
}

Vec3 Camera::getFront() const
{
  return CameraAxis(*this, 2, -1);
}

Vec3 Camera::getRight() const
{
  return CameraAxis(*this, 0, 1);
}

Vec3 Camera::getUp() const
{
  return CameraAxis(*this, 1, 1);
}

void Camera::lookAt(Vec3 target, Vec3 up)
{
  Detail::ValidateCameraFrame(Position(), target, up);
  m_Target = target;
  m_Up = up;
  setRotation({0, 0, 0});
}

void Camera::setPerspective(float fovDegrees, float nearPlane, float farPlane)
{
  if (!std::isfinite(fovDegrees) || fovDegrees <= 0.0f || fovDegrees >= 180.0f) {
    throw std::invalid_argument("Perspective FOV must be between zero and 180 degrees.");
  }
  Detail::ValidateClipPlanes(nearPlane, farPlane);
  m_FOV = fovDegrees;
  m_NearPlane = nearPlane;
  m_FarPlane = farPlane;
  m_Projection = Projection::Perspective;
}

void Camera::setOrthographic(float viewHeight, float nearPlane, float farPlane)
{
  Detail::ValidatePositive(viewHeight);
  Detail::ValidateClipPlanes(nearPlane, farPlane);
  m_ViewHeight = viewHeight;
  m_NearPlane = nearPlane;
  m_FarPlane = farPlane;
  m_Projection = Projection::Orthographic;
}

} // namespace Jade
