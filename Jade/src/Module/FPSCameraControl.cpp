#include "FPSCameraControl.hpp"

#include <Jade/Core/Scene.hpp>
#include <Jade/Graphics/Motion.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>

namespace Jade::Module {
namespace {

constexpr double degreesPerRadian = 180.0 / std::numbers::pi;

struct Angles {
  double yaw;
  double pitch;
};

Angles GetAngles(const Transform &pose)
{
  const auto front = Motion::getFront(pose);
  const auto right = Motion::getRight(pose);
  // At a vertical heading, use the right axis to preserve the azimuth.
  const double yaw = std::hypot(front.x, front.z) > 1e-6
                         ? std::atan2(front.x, -front.z)
                         : std::atan2(right.z, right.x);
  return {yaw * degreesPerRadian,
          std::asin(std::clamp(static_cast<double>(front.y), -1.0, 1.0)) * degreesPerRadian};
}

void SetAngles(Transform &pose, Angles angles)
{
  pose.rotation = (Quaternion::angleAxis({0, 1, 0}, -angles.yaw) *
                   Quaternion::angleAxis({1, 0, 0}, std::clamp(angles.pitch, -89.0, 89.0)))
                      .normalized();
}

struct ControlState {
  Scene &scene;
  Entity target;
  std::optional<Quaternion> appliedRotation;

  explicit ControlState(Scene &owner) : scene(owner) {}

  Entity Target()
  {
    const auto camera = scene.camera();
    if (camera != target) {
      target = camera;
      appliedRotation.reset();
    }
    if (camera.has<Transform>()) {
      auto &pose = camera.get<Transform>();
      // Re-read the pose so camera selection and external edits need no rebinding.
      // Leave unchanged quaternions alone to avoid drift and render invalidation.
      if (!appliedRotation || pose.rotation != *appliedRotation) {
        SetAngles(pose, GetAngles(pose));
        appliedRotation = pose.rotation;
      }
    } else {
      appliedRotation.reset();
    }
    return camera;
  }
};

template <KeyString Binding>
void BindMovement(Scene &scene, const std::shared_ptr<ControlState> &state,
                  float forward, float right, float speed = 3)
{
  scene.on<KeyHeld<Binding>>([state, forward, right, speed](const auto &event) {
    const auto camera = state->Target();
    if (!camera.has<Transform>())
      return;
    auto &pose = camera.get<Transform>();
    const double yaw = GetAngles(pose).yaw / degreesPerRadian;
    const Vec3 direction{static_cast<float>(forward * std::sin(yaw) + right * std::cos(yaw)),
                         0,
                         static_cast<float>(-forward * std::cos(yaw) + right * std::sin(yaw))};
    pose.position = pose.position + direction * (speed * event.dt);
  });
}

} // namespace

void FPSCameraControl::attach(Scene &scene)
{
  auto state = std::make_shared<ControlState>(scene);
  BindMovement<"w">(scene, state, 1, 0);
  BindMovement<"s">(scene, state, -1, 0);
  BindMovement<"a">(scene, state, 0, -1);
  BindMovement<"d">(scene, state, 0, 1);
  BindMovement<"Up">(scene, state, 1, 0);
  BindMovement<"Down">(scene, state, -1, 0);
  BindMovement<"Left">(scene, state, 0, -1);
  BindMovement<"Right">(scene, state, 0, 1);
  BindMovement<"S-w">(scene, state, 1, 0, 6);
  BindMovement<"C-w">(scene, state, 1, 0, 1);
  scene.on<MouseMoved>([state](const auto &mouse) {
    const auto camera = state->Target();
    if (!camera.has<Transform>())
      return;
    if (mouse.dx == 0 && mouse.dy == 0)
      return;
    auto &pose = camera.get<Transform>();
    auto angles = GetAngles(pose);
    angles.yaw += mouse.dx * 0.1;
    angles.pitch -= mouse.dy * 0.1;
    SetAngles(pose, angles);
    state->appliedRotation = pose.rotation;
  });
  scene.on<FrameUpdate>([state] { state->Target(); });
  scene.on<SceneActivated>([state] { state->Target(); });
}

} // namespace Jade::Module
