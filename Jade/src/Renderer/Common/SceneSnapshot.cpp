#include "SceneSnapshot.hpp"
#include "Math.hpp"
#include <Jade/Graphics/Geometry.hpp>
#include "../../internal/Core/SceneTransforms.hpp"
namespace Jade::Renderer {
SceneSnapshot SceneSnapshot::capture(const Scene &scene, const Assets &assets)
{
  SceneSnapshot result;
  Jade::Detail::SceneTransforms transforms(scene);
  const auto camera = scene.camera();
  if (camera.has<Transform>() && camera.has<Camera>()) {
    const auto view = transforms.CameraView(camera);
    const auto inverse = Math::inverseAffine(view);
    CameraFrame frame;
    frame.lens = camera.get<Camera>();
    Jade::Detail::ValidateCamera(frame.lens);
    frame.position = Math::point(inverse, {});
    frame.right = Math::unit(Math::vector(inverse, {1, 0, 0}));
    frame.up = Math::unit(Math::vector(inverse, {0, 1, 0}));
    frame.forward = Math::unit(Math::vector(inverse, {0, 0, -1}));
    result.camera = frame;
  }
  for (const auto entity : scene.query<Geometry>()) {
    const auto handle = entity.get<Geometry>().mesh;
    if (handle.empty())
      continue;
    if (!entity.has<Material>())
      throw std::invalid_argument("Geometry requires a Material component.");
    const auto material = entity.get<Material>();
    validateMaterial(material);
    const auto model = transforms.Resolve(entity).model;
    (void)Math::inverseAffine(model);
    result.instances.push_back({assets.share(handle), model, material, entity.id()});
  }
  return result;
}

} // namespace Jade::Renderer
