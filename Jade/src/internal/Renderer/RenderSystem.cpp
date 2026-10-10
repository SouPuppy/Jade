#include "RenderSystem.hpp"

#include <Jade/Core/Transform.hpp>
#include <Jade/Graphics/Camera.hpp>
#include <Jade/Graphics/MeshRenderer.hpp>

#include <array>

namespace Jade::Detail {

void RenderSystem::Render(World& world, const Assets& assets, Entity camera,
  Renderer& renderer, FramebufferSize size, bool axisGizmo)
{
  if (size.width <= 0 || size.height <= 0 || !camera.has<Transform>() || !camera.has<Camera>()) {
    return;
  }
  const auto viewProjection = ProjectionMatrix(camera.get<Camera>(), static_cast<double>(size.width) / size.height)
    * ViewMatrix(camera.get<Transform>());
  world.each<Transform, MeshRenderer>([&](Entity, const Transform& transform, const MeshRenderer& visual) {
    if (visual.mesh.empty()) {
      return;
    }
    const auto& mesh = assets.get(visual.mesh);
    Validate(visual.color);
    DrawParameters parameters;
    parameters.transform = viewProjection * ModelMatrix(transform);
    parameters.normalTransform = NormalMatrix(transform);
    parameters.color = visual.color;
    renderer.DrawMesh(mesh, parameters);
  });
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
