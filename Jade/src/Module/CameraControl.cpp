#include "CameraControl.hpp"

#include <Jade/Core/Scene.hpp>
#include <Jade/Graphics/Motion.hpp>

#include <memory>

namespace Jade::Module {
namespace {

struct ControlState {
  Scene& scene;
  Entity target;
  bool dragging = false;

  explicit ControlState(Scene& owner) : scene(owner), target(owner.camera()) {}

  Entity Target()
  {
    const auto selected = scene.camera();
    if (selected != target) {
      target = selected;
      dragging = false;
    }
    if (!selected.has<Transform>()) dragging = false;
    return selected;
  }

  void Reset()
  {
    target = scene.camera();
    dragging = false;
  }
};

template<KeyString Binding, class Action, class Speed>
void BindMotion(Scene& scene, const std::shared_ptr<ControlState>& state, Action action, Speed speed)
{
  scene.on<KeyHeld<Binding>>([state, action, speed](const auto& event) {
    const auto camera = state->Target();
    if (camera.has<Transform>()) action(camera.get<Transform>(), speed * event.dt);
  });
}

} // namespace

void CameraControl::attach(Scene& scene)
{
  auto state = std::make_shared<ControlState>(scene);
  BindMotion<"w">(scene, state, Motion::forward, 3.0f);
  BindMotion<"s">(scene, state, Motion::backward, 3.0f);
  BindMotion<"a">(scene, state, Motion::left, 3.0f);
  BindMotion<"d">(scene, state, Motion::right, 3.0f);
  BindMotion<"Up">(scene, state, Motion::forward, 3.0f);
  BindMotion<"Down">(scene, state, Motion::backward, 3.0f);
  BindMotion<"Left">(scene, state, Motion::left, 3.0f);
  BindMotion<"Right">(scene, state, Motion::right, 3.0f);
  BindMotion<"S-w">(scene, state, Motion::forward, 6.0f);
  BindMotion<"C-w">(scene, state, Motion::forward, 1.0f);
  BindMotion<"Space">(scene, state, Motion::up, 3.0f);
  BindMotion<"C-Space">(scene, state, Motion::down, 3.0f);
  BindMotion<"q">(scene, state, Motion::roll, -60.0);
  BindMotion<"e">(scene, state, Motion::roll, 60.0);

  scene.on<MousePressed<"Left">>([state] {
    const auto camera = state->Target();
    state->dragging = camera.has<Transform>();
  });
  scene.on<MouseReleased<"Left">>([state] { state->Reset(); });
  scene.on<MouseMoved>([state](const auto& mouse) {
    const auto camera = state->Target();
    if (state->dragging && camera.has<Transform>()) {
      Motion::yaw(camera.get<Transform>(), mouse.dx * 0.1);
      Motion::pitch(camera.get<Transform>(), -mouse.dy * 0.1);
    }
  });
  scene.on<MouseScrolled>([state](const auto& mouse) {
    const auto camera = state->Target();
    if (camera.has<Camera>()) Motion::zoom(camera.get<Camera>(), mouse.dy);
  });
  scene.on<SceneActivated>([state] { state->Reset(); });
  scene.on<SceneDeactivated>([state] { state->Reset(); });
}

} // namespace Jade::Module
