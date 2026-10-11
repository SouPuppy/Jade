#include "PlayerController.hpp"
#include <cmath>
#include <stdexcept>

namespace Untitled {
void PlayerController::update(Jade::Scene& scene, Jade::Entity player, const CameraController& camera, Jade::Vec2 input, float dt) {
  if (!std::isfinite(dt) || dt < 0 || !std::isfinite(input.x) || !std::isfinite(input.y))
    throw std::invalid_argument("Invalid player input or dt.");
  const float magnitude = std::hypot(input.x, input.y);
  state = magnitude > 0 ? PlayerState::Walking : PlayerState::Idle;
  if (magnitude == 0) return;
  const auto direction = (camera.right() * input.x + camera.forward() * input.y) * (1 / magnitude);
  facing = direction;
  const auto actual = player.get<Jade::CharacterController>().move(scene, player, direction * (speed * dt));
  if (dt > 0 && std::hypot(actual.x, actual.z) < 0.00001f) state = PlayerState::Idle;
}
} // namespace Untitled
