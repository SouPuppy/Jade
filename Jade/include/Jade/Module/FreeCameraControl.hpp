#pragma once

namespace Jade {

class Scene;

namespace Module {

// Installs scene-local free flight with left-button mouse look, roll and zoom.
// Input always targets the Scene's currently selected camera.
// A Scene may install either FreeCameraControl or FPSCameraControl.
struct FreeCameraControl {
  static void attach(Scene& scene);
};

} // namespace Module
} // namespace Jade
