#include "CameraController.hpp"
#include <cmath>
#include <numbers>

namespace Untitled {
namespace {
float angle(int step) { return step * std::numbers::pi_v<float> / 4; }
}
void CameraController::rotate(int delta) { step = ((step + delta) % 8 + 8) % 8; }
Jade::Vec3 CameraController::forward() const { return {-std::sin(angle(step)), 0, -std::cos(angle(step))}; }
Jade::Vec3 CameraController::right() const { return {std::cos(angle(step)), 0, -std::sin(angle(step))}; }
void CameraController::update(Jade::Entity camera, Jade::Vec3 target) const {
  auto& pose = camera.get<Jade::Transform>();
  const auto look = target + Jade::Vec3{0, 0.7f, 0};
  pose.position = look - forward() * (12 / std::sqrt(2.0f)) + Jade::Vec3{0, 12 / std::sqrt(2.0f), 0};
  Jade::Motion::lookAt(pose, look);
}
int relativeDirection(Jade::Vec3 facing, const CameraController& camera) {
  const float x = Jade::dot(facing, camera.right()), y = Jade::dot(facing, camera.forward());
  return (static_cast<int>(std::lround(std::atan2(x, y) * 4 / std::numbers::pi_v<float>)) + 8) % 8;
}
} // namespace Untitled
