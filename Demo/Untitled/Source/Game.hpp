#pragma once
#include "PlayerController.hpp"
#include <array>
#include <filesystem>

namespace Untitled {
// Game must outlive its active Scene callbacks and Application::Run.
class Game {
public:
  Game(Jade::Runtime& runtime, const std::filesystem::path& assetRoot);
  std::shared_ptr<Jade::Scene> scene() const { return m_Scene; }
  Jade::Entity player() const { return m_Player; }
  void update(Jade::Vec2 input, float dt);
  void queueInput(Jade::Vec2 input) { m_Input = input; }
  void rotate(int steps);
  void reset();
private:
  Jade::Runtime& m_Runtime;
  std::shared_ptr<Jade::Scene> m_Scene;
  Jade::Entity m_Player, m_Camera;
  CameraController m_CameraControl;
  PlayerController m_PlayerControl;
  Jade::Vec2 m_Input;
  std::array<Jade::AnimationClipHandle, 8> m_Idle, m_Walk;
  PlayerState m_PreviousState = PlayerState::Idle;
  void selectAnimation();
};
} // namespace Untitled
