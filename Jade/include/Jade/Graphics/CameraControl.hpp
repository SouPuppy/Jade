#pragma once

#include <Jade/Input/Key.hpp>
#include <Jade/Input/MouseButton.hpp>
#include <array>

namespace Jade {

// Interaction style, independent of projection and of the camera being viewed.
enum class ViewMode { Editor, FirstPerson };
enum class CameraAction { Forward, Backward, Left, Right, Ascend, Descend, Count };

struct CameraBindings {
  // Physical keys: modifiers never disable movement. Unknown disables a binding.
  std::array<std::array<Key, 2>, static_cast<unsigned>(CameraAction::Count)> movement{{
    {Key::W, Key::Up}, {Key::S, Key::Down}, {Key::A, Key::Left},
    {Key::D, Key::Right}, {Key::Space, Key::Unknown}, {Key::LeftShift, Key::RightShift}
  }};
  MouseButton look = MouseButton::Right;
  MouseButton pan = MouseButton::Middle;
  bool wheelZoom = true;
  constexpr auto& operator[](CameraAction action) { return movement.at(static_cast<unsigned>(action)); }
  constexpr const auto& operator[](CameraAction action) const { return movement.at(static_cast<unsigned>(action)); }
  bool operator==(const CameraBindings&) const = default;
};

// Optional component on a camera entity; FPSCameraControl uses these defaults
// when absent. Editor and FirstPerson share the same upright movement model.
struct CameraControl {
  ViewMode viewMode = ViewMode::Editor;
  CameraBindings bindings;
  float speed = 3;
  float slowSpeed = 1;
  Modifiers slowModifier = Modifiers::Control;
  double sensitivity = 0.1; // Degrees per logical cursor pixel.
  float panDistance = 10;  // Perspective pan plane distance in parent units.
  bool operator==(const CameraControl&) const = default;
};

} // namespace Jade
