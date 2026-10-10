#include "Motion.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Jade::Motion {
namespace {

void Move(Camera& camera, Vec3 direction, float distance)
{
  if (!std::isfinite(distance)) {
    throw std::invalid_argument("Movement distance must be finite.");
  }
  camera.setPos(camera.getPos() + direction * distance);
}

void ValidateAngle(double degrees)
{
  if (!std::isfinite(degrees)) {
    throw std::invalid_argument("Rotation increment must be finite.");
  }
}

} // namespace

void forward(Camera& camera, float distance) { Move(camera, camera.getFront(), distance); }
void backward(Camera& camera, float distance) { Move(camera, -camera.getFront(), distance); }
void right(Camera& camera, float distance) { Move(camera, camera.getRight(), distance); }
void left(Camera& camera, float distance) { Move(camera, -camera.getRight(), distance); }
void up(Camera& camera, float distance) { Move(camera, {0, 1, 0}, distance); }
void down(Camera& camera, float distance) { Move(camera, {0, -1, 0}, distance); }

void yaw(Camera& camera, double degrees)
{
  ValidateAngle(degrees);
  auto rotation = camera.Rotation();
  rotation.y = static_cast<float>(std::remainder(
    static_cast<double>(rotation.y) - std::remainder(degrees, 360.0), 360.0));
  camera.setRotation(rotation);
}

void pitch(Camera& camera, double degrees)
{
  ValidateAngle(degrees);
  auto rotation = camera.Rotation();
  rotation.x = static_cast<float>(std::clamp(static_cast<double>(rotation.x) + degrees, -89.0, 89.0));
  camera.setRotation(rotation);
}

void roll(Camera& camera, double degrees)
{
  ValidateAngle(degrees);
  auto rotation = camera.Rotation();
  rotation.z = static_cast<float>(std::remainder(
    static_cast<double>(rotation.z) - std::remainder(degrees, 360.0), 360.0));
  camera.setRotation(rotation);
}

void zoom(Camera& camera, double scroll)
{
  if (!std::isfinite(scroll)) {
    throw std::invalid_argument("Zoom increment must be finite.");
  }
  if (scroll == 0) {
    return;
  }
  const double factor = std::exp(-std::clamp(scroll, -100.0, 100.0) * 0.1);
  if (camera.Mode() == Projection::Perspective) {
    const auto fov = static_cast<float>(std::clamp(camera.FOV() * factor, 1.0, 179.0));
    camera.setPerspective(fov, camera.NearPlane(), camera.FarPlane());
  } else {
    const auto height = static_cast<float>(std::clamp(camera.ViewHeight() * factor, 0.01, 10000.0));
    camera.setOrthographic(height, camera.NearPlane(), camera.FarPlane());
  }
}

} // namespace Jade::Motion
