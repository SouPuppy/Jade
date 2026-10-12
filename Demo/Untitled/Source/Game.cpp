#include "Game.hpp"
#include "World.hpp"
#include <string>
#include <stdexcept>
#include <format>

namespace Untitled {
Game::Game(Jade::Runtime& runtime, const std::filesystem::path& assetRoot, const std::filesystem::path& saveRoot)
  : m_Runtime(runtime), m_Scene(std::make_shared<Jade::Scene>(std::make_shared<Jade::World>())) {
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
  std::shared_ptr<WorldStore> store;
  if(!saveRoot.empty()) store=std::make_shared<FileWorldStore>(saveRoot);
  configureWorld(m_Scene->world(),assets,std::filesystem::exists(assetRoot.parent_path()/"UntitledWorld/WorldConfig.json") ? assetRoot.parent_path()/"UntitledWorld" : assetRoot.parent_path()/"World",store,&runtime.jobs());
  m_Source=m_Scene->world().streaming().addSource({{136,0,136}});
  m_Player = m_Scene->world().create<Archetype<Transform, SpriteRenderer, Animator, Collider, CharacterController>>();
  m_Player.get<SpriteRenderer>().size = {4.2f,4.2f};
  m_Player.get<SpriteRenderer>().offset.y = -0.35f;
  m_Player.get<Collider>() = Collider{Box{{0.22f,0.35f,0.22f}}};
  m_Player.get<CharacterController>().groundHeight = 0.35f;
  m_Camera = m_Scene->world().create<Archetype<Transform, Camera>>();
  auto& lens = m_Camera.get<Camera>();
  lens.projection = Projection::Orthographic;
  lens.viewHeight = 16;
  lens.farPlane = 100;
  m_Scene->setCamera(m_Camera);
  listen<KeyHeld<"W">>([this] { m_Input.y += 1; });
  listen<KeyHeld<"S">>([this] { m_Input.y -= 1; });
  listen<KeyHeld<"A">>([this] { m_Input.x -= 1; });
  listen<KeyHeld<"D">>([this] { m_Input.x += 1; });
  listen<KeyPressed<"Q">>([this] { rotate(-1); });
  listen<KeyPressed<"E">>([this] { rotate(1); });
  listen<KeyPressed<"F">>([this] { if (!m_Runtime.paused()) interact(); });
  listen<KeyPressed<"X">>([this] { if (!m_Runtime.paused()) interact(true); });
  listen<KeyPressed<"R">>([this] { reset(); });
  listen<SceneDeactivated>([this] { m_Input = {}; m_FrameInput = {}; m_Runtime.renderer().setOverlayText({}); });
  listen<FrameUpdate>([this](const FrameUpdate& frame) {
    (void)frame;
    m_FrameInput = m_Input;
    m_Input = {};
    const auto coord=Jade::chunkAt(m_Player.get<Jade::Transform>().position);
    const auto& stats=m_Scene->world().streaming().statistics();
    m_Runtime.renderer().setOverlayText(std::format("CHUNK: {},{}\nDATA: {} RENDER: {} COLLISION: {} ACTIVE: {}\nLOADING: {} FROZEN: {} SAVE: {}\nQ/E ROTATE  F PUSH  X DELETE  R RESET{}",
      coord.x,coord.z,stats.data,stats.rendered,stats.collision,stats.active,stats.loading,stats.frozen,
      stats.saving?"SAVING":"READY",stats.error.empty()?std::string{}:"\nERROR: "+stats.error));
  });
  reset();
  m_Scene->world().streaming().barrier();
  m_Scene->world().setFixedUpdate([this](Jade::World&, float dt) { update(m_FrameInput, dt); });
  runtime.addWorld(m_Scene->worldHandle());
}
Game::~Game() {
  m_Scene->world().setFixedUpdate({});
  for(auto& connection:m_Connections) connection.disconnect();
}
void Game::selectAnimation() {
  const auto direction = relativeDirection(m_PlayerControl.facing, m_CameraControl);
  auto& animator = m_Player.get<Jade::Animator>();
  const bool walking = m_PlayerControl.state == PlayerState::Walking;
  animator.play(walking ? m_Walk[direction] : m_Idle[direction], m_PlayerControl.state == m_PreviousState);
  m_PreviousState = m_PlayerControl.state;
}
void Game::update(Jade::Vec2 input, float dt) {
  auto& world=m_Scene->world();
  auto& pose=m_Player.get<Jade::Transform>();
  world.streaming().setSource(m_Source,{pose.position});
  const auto proposed=pose.position+(m_CameraControl.right()*input.x+m_CameraControl.forward()*input.y)*(m_PlayerControl.speed*dt);
  if(world.streaming().ready(Jade::chunkAt(proposed)) && world.streaming().ready(Jade::chunkAt(pose.position)))
    m_PlayerControl.update(world,m_Player,m_CameraControl,input,dt);
  else m_PlayerControl.state=PlayerState::Idle;
  pose.position.x=std::clamp(pose.position.x,.25f,255.75f);pose.position.z=std::clamp(pose.position.z,.25f,255.75f);
  world.streaming().setSource(m_Source,{pose.position});
  m_CameraControl.update(m_Camera, m_Player.get<Jade::Transform>().position);
  selectAnimation();
}
void Game::rotate(int steps) {
  m_CameraControl.rotate(steps);
  m_CameraControl.update(m_Camera, m_Player.get<Jade::Transform>().position);
  selectAnimation();
}
void Game::reset() {
  m_Player.get<Jade::Transform>().position = {136,0.35f,136};
  m_PlayerControl = {};
  m_CameraControl = {};
  m_Input = {};
  m_FrameInput = {};
  m_Player.get<Jade::Animator>() = {};
  m_PreviousState = PlayerState::Idle;
  update({}, 0);
  m_Runtime.animation().update(m_Scene->world(), m_Runtime.assets(), 0);
}
void Game::interact(bool erase) {
  using namespace Jade;
  auto& world=m_Scene->world();Entity target;float nearest=5;
  const auto position=m_Player.get<Transform>().position;
  for(auto e:world.registry().query<Transform,BallVisual,RigidBody>()) {
    if(e.has<Residency>()&&!e.get<Residency>().simulation)continue;
    const auto delta=e.get<Transform>().position-position;const float distance=std::hypot(delta.x,delta.z);
    if(distance<nearest){nearest=distance;target=e;}
  }
  if(!target)return;
  if(erase)world.destroy(target);
  else {
    auto direction=target.get<Transform>().position-position;direction.y=0;
    const auto length=std::hypot(direction.x,direction.z);
    direction=length>1e-4f?direction*(1/length):m_CameraControl.forward();
    m_Runtime.physics().applyImpulse(world,target,direction*5+Vec3{0,2,0},target.get<Transform>().position);
  }
}
} // namespace Untitled
