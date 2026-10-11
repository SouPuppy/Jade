#pragma once

namespace Jade {

class Scene;

namespace Module {

// Installs scene-local free flight, left-button look, wheel zoom and
// orthographic middle-button panning. Q/E are unbound.
// Input always targets the Scene's currently selected camera.
// A Scene may install either FreeCameraControl or FPSCameraControl.
struct FreeCameraControl {
  static void attach(Scene& scene);
};

} // namespace Module
} // namespace Jade
