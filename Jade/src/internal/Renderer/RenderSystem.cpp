#include "RenderSystem.hpp"
#include "SceneTransforms.hpp"

#include <Jade/Core/Transform.hpp>
#include <Jade/Graphics/Camera.hpp>
#include <Jade/Graphics/MeshRenderer.hpp>

#include <array>

namespace Jade::Detail {

void RenderSystem::Render(const Scene& scene, const Assets& assets,
  Renderer& renderer, FramebufferSize size, bool axisGizmo)
{
  const auto camera = scene.camera();
  if (size.width <= 0 || size.height <= 0 || !camera.has<Transform>() || !camera.has<Camera>()) {
    return;
  }
  SceneTransforms transforms(scene);
  const auto viewProjection = ProjectionMatrix(camera.get<Camera>(), static_cast<double>(size.width) / size.height)
    * transforms.CameraView(camera);
  for (const auto entity : scene.view<Transform, MeshRenderer>()) {
    const auto& visual = entity.get<MeshRenderer>();
    if (visual.mesh.empty()) {
      continue;
    }
    const auto& mesh = assets.get(visual.mesh);
    Validate(visual.color);
    DrawParameters parameters;
    const auto& pose = transforms.Resolve(entity);
    parameters.transform = viewProjection * pose.model;
    parameters.normalTransform = pose.normal;
    parameters.color = visual.color;
    renderer.DrawMesh(mesh, parameters);
  }
  if (axisGizmo) {
    // Internal debug resources, shared across frames. No world component storage.
    static const std::array<Mesh, 3> axes{{
      Mesh::line({0, 0, 0}, {1, 0, 0}), Mesh::line({0, 0, 0}, {0, 1, 0}), Mesh::line({0, 0, 0}, {0, 0, 1})
    }};
    constexpr std::array<Color, 3> colors{{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}}};
    DrawParameters parameters;
    parameters.transform = viewProjection;
    parameters.depthTest = false;
    parameters.depthWrite = false;
    for (std::size_t axis = 0; axis < axes.size(); ++axis) {
      parameters.color = colors[axis];
      renderer.DrawMesh(axes[axis], parameters);
    }
  }
}

} // namespace Jade::Detail
