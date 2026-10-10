#pragma once

namespace Jade {

class Scene;

namespace Module {

// Installs scene-local free camera controls. Use scene.use<CameraControl>().
// Input always targets the Scene's currently selected camera.
struct CameraControl {
  static void attach(Scene& scene);
};

} // namespace Module
} // namespace Jade
