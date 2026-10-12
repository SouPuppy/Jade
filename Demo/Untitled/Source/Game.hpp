#pragma once
#include "PlayerController.hpp"
#include <array>
#include <filesystem>

namespace Untitled {
// Game must outlive its active Scene callbacks and Application::Run.
class Game {
public:
  Game(Jade::Runtime& runtime, const std::filesystem::path& assetRoot, const std::filesystem::path& saveRoot = {});
  ~Game();
  std::shared_ptr<Jade::Scene> scene() const { return m_Scene; }
  Jade::Entity player() const { return m_Player; }
  void update(Jade::Vec2 input, float dt);
  void queueInput(Jade::Vec2 input) { m_Input = input; }
  void rotate(int steps);
  void reset();
  void interact(bool erase = false);
private:
  Jade::Runtime& m_Runtime;
  std::shared_ptr<Jade::Scene> m_Scene;
  Jade::Entity m_Player, m_Camera;
  CameraController m_CameraControl;
  PlayerController m_PlayerControl;
  Jade::Vec2 m_Input;
  Jade::Vec2 m_FrameInput;
  std::uint64_t m_Source = 0;
  std::vector<Jade::EventConnection> m_Connections;
  template<class Event,class Callback> void listen(Callback&& callback) {
    m_Connections.push_back(m_Scene->on<Event>(std::forward<Callback>(callback)));
  }
  std::array<Jade::AnimationClipHandle, 8> m_Idle, m_Walk;
  PlayerState m_PreviousState = PlayerState::Idle;
  void selectAnimation();
};
} // namespace Untitled
