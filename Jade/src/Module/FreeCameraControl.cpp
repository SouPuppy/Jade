#include "FreeCameraControl.hpp"

#include <Jade/Core/Scene.hpp>
#include <Jade/Graphics/Motion.hpp>
#include "../internal/Math/Validation.hpp"

#include <memory>
#include <algorithm>
#include <optional>

namespace Jade::Module {
namespace {

struct ControlState {
  Scene &scene;
  Entity target;
  bool dragging = false, panning = false;
  struct Zoom {
    Camera reference;
    double scroll = 0;
    float applied = 0;
  };
  std::optional<Zoom> perspectiveZoom, orthographicZoom;

  void ZoomCamera(Camera &camera, double scroll)
  {
    auto &state = camera.projection == Projection::Perspective ? perspectiveZoom : orthographicZoom;
    const float current =
        camera.projection == Projection::Perspective ? camera.fovDegrees : camera.viewHeight;
    // A user edit establishes a new reference. Retain unapplied scroll at limits,
    // so reversing a wheel gesture restores its original projection.
    if (!state || state->applied != current)
      state = Zoom{camera, 0, current};
    auto next = *state;
    next.scroll += std::clamp(scroll, -100.0, 100.0);
    auto lens = camera;
    if (camera.projection == Projection::Perspective)
      lens.fovDegrees = next.reference.fovDegrees;
    else
      lens.viewHeight = next.reference.viewHeight;
    Motion::zoom(lens, next.scroll);
    if (camera.projection == Projection::Perspective)
      camera.fovDegrees = lens.fovDegrees;
    else
      camera.viewHeight = lens.viewHeight;
    next.applied = camera.projection == Projection::Perspective ? camera.fovDegrees : camera.viewHeight;
    state = next;
  }

  explicit ControlState(Scene &owner) : scene(owner), target(owner.camera()) {}

  Entity Target()
  {
    const auto selected = scene.camera();
    if (selected != target) {
      target = selected;
      perspectiveZoom.reset();
      orthographicZoom.reset();
      dragging = panning = false;
    }
    if (!selected.has<Transform>())
      dragging = panning = false;
    return selected;
  }

  void Reset()
  {
    Target();
    dragging = panning = false;
  }
};

template <KeyString Binding, class Action, class Speed>
void BindMotion(Scene &scene, const std::shared_ptr<ControlState> &state, Action action, Speed speed)
{
  scene.on<KeyHeld<Binding>>([state, action, speed](const auto &event) {
    const auto camera = state->Target();
    if (camera.has<Transform>())
      action(camera.get<Transform>(), speed * event.dt);
  });
}

} // namespace

void FreeCameraControl::attach(Scene &scene)
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

  scene.on<MousePressed<"Left">>([state] {
    const auto camera = state->Target();
    state->dragging = camera.has<Transform>();
  });
  scene.on<MouseReleased<"Left">>([state] { state->dragging = false; });
  scene.on<MousePressed<"Middle">>([state] {
    const auto camera = state->Target();
    state->panning = camera.has<Transform>() && camera.has<Camera>() &&
                     camera.get<Camera>().projection == Projection::Orthographic;
  });
  scene.on<MouseReleased<"Middle">>([state] { state->panning = false; });
  scene.on<MouseMoved>([state](const auto &mouse) {
    const auto camera = state->Target();
    if (state->panning && camera.has<Transform>() && camera.has<Camera>()) {
      const auto& lens = camera.get<Camera>();
      if (lens.projection != Projection::Orthographic) {
        state->panning = false;
      } else if (mouse.windowHeight > 0) {
        Detail::ValidateCamera(lens);
        auto& pose = camera.get<Transform>();
        const double units = static_cast<double>(lens.viewHeight) / mouse.windowHeight;
        const Vec3 shift = Motion::getRight(pose) * static_cast<float>(-mouse.dx * units) +
                           Motion::getUp(pose) * static_cast<float>(mouse.dy * units);
        const auto next = pose.position + shift;
        Detail::Validate(next);
        pose.position = next;
      }
    } else if (state->dragging && camera.has<Transform>()) {
      Motion::yaw(camera.get<Transform>(), mouse.dx * 0.1);
      Motion::pitch(camera.get<Transform>(), -mouse.dy * 0.1);
    }
  });
  scene.on<MouseScrolled>([state](const auto &mouse) {
    const auto camera = state->Target();
    if (camera.has<Camera>())
      state->ZoomCamera(camera.get<Camera>(), mouse.dy);
  });
  scene.on<SceneActivated>([state] { state->Reset(); });
  scene.on<SceneDeactivated>([state] { state->Reset(); });
}

} // namespace Jade::Module
