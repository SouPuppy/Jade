#pragma once

namespace Jade {

class Scene;

namespace Module {

// Horizontal movement and captured mouse look, with pitch limited to +/-89 degrees
// and no roll. Movement and yaw use parent Y (world Y for root cameras).
// Jump, gravity and collision belong to a future character controller.
// A Scene may install either FreeCameraControl or FPSCameraControl.
struct FPSCameraControl {
  static void attach(Scene& scene);
};

} // namespace Module
} // namespace Jade
