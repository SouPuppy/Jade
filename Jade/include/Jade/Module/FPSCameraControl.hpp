#pragma once

namespace Jade {

class Scene;

namespace Module {

// The single upright camera controller. Defaults to editor interaction:
// WASD/arrows, Space up, Shift down, Ctrl slow, right-drag look, middle-drag pan,
// wheel zoom. Optional CameraControl on the selected camera configures bindings,
// speed and Editor/FirstPerson interaction. No gravity or collision is applied.
struct FPSCameraControl {
  static void attach(Scene& scene);
};

} // namespace Module
} // namespace Jade
