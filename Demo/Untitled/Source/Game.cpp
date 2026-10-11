#include "Game.hpp"
#include "World.hpp"
#include <string>
#include <stdexcept>

namespace Untitled {
Game::Game(Jade::Runtime& runtime, const std::filesystem::path& assetRoot)
  : m_Runtime(runtime), m_Scene(std::make_shared<Jade::Scene>()) {
  using namespace Jade;
  auto& assets = runtime.assets();
  const std::array<const char*,8> directions = {"Up","UpRight","Right","DownRight","Down","DownLeft","Left","UpLeft"};
  for (int direction = 0; direction < 8; ++direction) {
    auto load = [&](const char* action, int frameCount, int rows) {
      const auto filename = std::string("GirlSample_") + action + "_" + directions[direction] + ".png";
      const auto texture = assets.load<Texture>(assetRoot / "Characters/Hero/Original" / filename);
      const auto& source = assets.get(texture);
      if (source.width() != 1024 || source.height() != rows * 256)
        throw std::runtime_error("Unexpected sprite sheet dimensions: " + filename);
      AnimationClip clip;
      for (int frame = 0; frame < frameCount; ++frame)
        clip.frames.push_back({texture, {static_cast<float>(frame % 4) / 4, static_cast<float>(frame / 4) / rows,
          0.25f, 1.0f / rows}, 1.0f / 12, {0.5f, 90.0f / 256}});
      return assets.add(std::move(clip));
    };
    m_Walk[direction] = load("Walk", 9, 3);
    m_Idle[direction] = load("ReadyIdle", 14, 4);
  }
  buildWorld(*m_Scene, assets);
  m_Player = m_Scene->create<Archetype<Transform, SpriteRenderer, Animator, Collider, CharacterController>>();
  m_Player.get<SpriteRenderer>().size = {4.2f,4.2f};
  m_Player.get<SpriteRenderer>().offset.y = -0.35f;
  m_Player.get<Collider>() = Collider{Box{{0.22f,0.35f,0.22f}}};
  m_Player.get<CharacterController>().groundHeight = 0.35f;
  m_Camera = m_Scene->create<Archetype<Transform, Camera>>();
  auto& lens = m_Camera.get<Camera>();
  lens.projection = Projection::Orthographic;
  lens.viewHeight = 8;
  lens.farPlane = 100;
  m_Scene->setCamera(m_Camera);
  m_Scene->on<KeyHeld<"W">>([this] { m_Input.y += 1; });
  m_Scene->on<KeyHeld<"S">>([this] { m_Input.y -= 1; });
  m_Scene->on<KeyHeld<"A">>([this] { m_Input.x -= 1; });
  m_Scene->on<KeyHeld<"D">>([this] { m_Input.x += 1; });
  m_Scene->on<KeyPressed<"Q">>([this] { rotate(-1); });
  m_Scene->on<KeyPressed<"E">>([this] { rotate(1); });
  m_Scene->on<KeyPressed<"R">>([this] { reset(); });
  m_Scene->on<SceneDeactivated>([this] { m_Input = {}; });
  m_Scene->on<FrameUpdate>([this](const FrameUpdate& frame) {
    if (!m_Runtime.paused()) update(m_Input, frame.dt);
    m_Input = {};
  });
  reset();
}
void Game::selectAnimation() {
  const auto direction = relativeDirection(m_PlayerControl.facing, m_CameraControl);
  auto& animator = m_Player.get<Jade::Animator>();
  const bool walking = m_PlayerControl.state == PlayerState::Walking;
  animator.play(walking ? m_Walk[direction] : m_Idle[direction], m_PlayerControl.state == m_PreviousState);
  m_PreviousState = m_PlayerControl.state;
}
void Game::update(Jade::Vec2 input, float dt) {
  m_PlayerControl.update(*m_Scene, m_Player, m_CameraControl, input, dt);
  m_CameraControl.update(m_Camera, m_Player.get<Jade::Transform>().position);
  selectAnimation();
}
void Game::rotate(int steps) {
  m_CameraControl.rotate(steps);
  m_CameraControl.update(m_Camera, m_Player.get<Jade::Transform>().position);
  selectAnimation();
}
void Game::reset() {
  m_Player.get<Jade::Transform>().position = {0,0.35f,0};
  m_PlayerControl = {};
  m_CameraControl = {};
  m_Input = {};
  m_Player.get<Jade::Animator>() = {};
  m_PreviousState = PlayerState::Idle;
  update({}, 0);
  m_Runtime.animation().update(*m_Scene, m_Runtime.assets(), 0);
}
} // namespace Untitled
