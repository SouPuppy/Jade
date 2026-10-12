#pragma once
#include "CameraController.hpp"

namespace Untitled {
enum class PlayerState { Idle, Walking };
class PlayerController {
public:
  PlayerState state = PlayerState::Idle;
  Jade::Vec3 facing{0,0,1};
  float speed = 3;
  void update(Jade::World& scene, Jade::Entity player, const CameraController& camera, Jade::Vec2 input, float dt);
};
} // namespace Untitled
