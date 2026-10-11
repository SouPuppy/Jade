#include "FPSCameraControl.hpp"
#include "../Core/Scene.hpp"

namespace Jade::Module {
namespace {
struct ControlState {
  Scene& scene;
  Entity target;
  std::shared_ptr<Detail::CameraNavigation> navigation;
  CameraControl defaults;

  Entity Target()
  {
    if (navigation->suspended()) return {};
    const auto selected = scene.camera();
    if (selected != target) { navigation->reset(); target = selected; }
    if (selected.has<Transform>()) navigation->synchronize(selected.get<Transform>(), Settings(selected),
      selected.has<Camera>() ? &selected.get<Camera>() : nullptr);
    else navigation->reset();
    return selected;
  }
  const CameraControl& Settings(Entity camera) const
  {
    return camera.has<CameraControl>() ? camera.get<CameraControl>() : defaults;
  }
};
}

void FPSCameraControl::attach(Scene& scene)
{
  auto navigation = std::make_shared<Detail::CameraNavigation>();
  auto state = std::make_shared<ControlState>(ControlState{scene, {}, navigation, {}});
  scene.on(KeyAction::Held, [state](const KeyEvent& event) {
    const auto camera = state->Target();
    if (camera.has<Transform>()) state->navigation->key(KeyAction::Held, event, state->Settings(camera));
  });
  for (const auto action : {MouseAction::Pressed, MouseAction::Released}) {
    scene.on(action, [state, action](const MouseEvent& event) {
      const auto camera = state->Target();
      if (camera.has<Transform>()) state->navigation->button(action, event, state->Settings(camera));
    });
  }
  scene.on<MouseMoved>([state](const auto& event) {
    const auto camera = state->Target();
    if (camera.has<Transform>()) state->navigation->mouse(camera.get<Transform>(),
      camera.has<Camera>() ? &camera.get<Camera>() : nullptr, event, state->Settings(camera));
  });
  scene.on<MouseScrolled>([state](const auto& event) {
    const auto camera = state->Target();
    if (camera.has<Camera>() && state->Settings(camera).bindings.wheelZoom)
      state->navigation->scroll(camera.get<Camera>(), event.dy);
  });
  scene.on<FrameUpdate>([state](const auto& event) {
    const auto camera = state->Target();
    if (camera.has<Transform>()) state->navigation->update(camera.get<Transform>(), event.dt, state->Settings(camera));
  });
  scene.on<SceneActivated>([navigation] { navigation->reset(); });
  scene.on<SceneDeactivated>([navigation] { navigation->reset(); });
  scene.m_Impl->cameraNavigation = std::move(navigation);
}
} // namespace Jade::Module
