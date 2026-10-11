#include "SceneSnapshot.hpp"
#include "Math.hpp"
#include <Jade/Graphics/Geometry.hpp>
#include <Jade/Graphics/SpriteRenderer.hpp>
#include "../../internal/Core/SceneTransforms.hpp"
namespace Jade::Renderer {
SceneSnapshot SceneSnapshot::capture(const Scene &scene, const Assets &assets, bool includeGeometry, bool includeDebug)
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
  if(!includeGeometry && !includeDebug) return result;
  for (const auto entity : scene.query<Transform>()) {
    if(!includeDebug && !entity.has<Light>()) continue;
    Matrix4 model;
    if(includeGeometry && entity.has<Light>()) model=transforms.Resolve(entity).model;
    else {
      // Diagnostic axes must not validate inactive/empty ECS components.
      try {model=transforms.Resolve(entity).model;} catch(const std::invalid_argument&) {continue;}
    }
    if(includeDebug) result.entityTransforms.push_back(model);
    if (includeDebug && entity.has<Camera>() && entity != camera) {
      const auto inverse = Math::inverseAffine(transforms.CameraView(entity));
      CameraFrame frame;
      frame.lens = entity.get<Camera>();
      Jade::Detail::ValidateCamera(frame.lens);
      frame.position = Math::point(inverse, {});
      frame.right = Math::unit(Math::vector(inverse, {1,0,0}));
      frame.up = Math::unit(Math::vector(inverse, {0,1,0}));
      frame.forward = Math::unit(Math::vector(inverse, {0,0,-1}));
      result.cameras.push_back(frame);
    }
    if (includeGeometry && entity.has<Light>()) {
      const auto& light = entity.get<Light>();
      if ((light.type!=LightType::Directional && light.type!=LightType::Point && light.type!=LightType::Area) || !light.intensity.finite() || light.intensity.r < 0 || light.intensity.g < 0 || light.intensity.b < 0 ||
          !std::isfinite(light.size.x) || !std::isfinite(light.size.y) || light.size.x <= 0 || light.size.y <= 0)
        throw std::invalid_argument("Invalid Light intensity or size.");
      result.lights.push_back({light.type, Math::point(model, {}), Math::unit(Math::vector(model,{0,0,-1})),
        Math::unit(Math::vector(model,{1,0,0})), Math::unit(Math::vector(model,{0,1,0})), light.intensity,
        {light.size.x * Math::length(Math::vector(model,{1,0,0})), light.size.y * Math::length(Math::vector(model,{0,1,0}))}, light.twoSided});
    }
  }
  if (!includeGeometry) return result;
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
  static const auto quad = std::make_shared<const Mesh>(Mesh::compile(MeshData{
    {{{-0.5f,0,0},{0,0,1}}, {{0.5f,0,0},{0,0,1}}, {{0.5f,1,0},{0,0,1}}, {{-0.5f,1,0},{0,0,1}}},
    {0,1,2,0,2,3}}));
  for (const auto entity : scene.query<Transform, SpriteRenderer>()) {
    const auto& sprite = entity.get<SpriteRenderer>();
    if (sprite.frame.texture.empty()) continue;
    const auto& region = sprite.frame.region;
    if (!std::isfinite(sprite.size.x) || !std::isfinite(sprite.size.y) || sprite.size.x <= 0 || sprite.size.y <= 0 ||
        !std::isfinite(sprite.alphaCutoff) || sprite.alphaCutoff < 0 || sprite.alphaCutoff > 1 ||
        !std::isfinite(region.x) || !std::isfinite(region.y) || !std::isfinite(region.width) || !std::isfinite(region.height) ||
        region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
        region.x + region.width > 1 || region.y + region.height > 1 ||
        !std::isfinite(sprite.frame.pivot.x) || !std::isfinite(sprite.frame.pivot.y) ||
        !std::isfinite(sprite.offset.x) || !std::isfinite(sprite.offset.y))
      throw std::invalid_argument("Invalid SpriteRenderer size, frame region, anchor or alpha cutoff.");
    auto model = transforms.Resolve(entity).model;
    if (sprite.billboard && result.camera) {
      auto right = result.camera->right;
      right.y = 0;
      right = Math::unit(right);
      const auto normal = cross(right, Vec3{0,1,0});
      const float width = Math::length(Math::vector(model, {1,0,0}));
      const float height = Math::length(Math::vector(model, {0,1,0}));
      for (int row = 0; row < 3; ++row) {
        model.at(row, 0) = (row == 0 ? right.x : row == 1 ? right.y : right.z) * width;
        model.at(row, 1) = row == 1 ? height : 0;
        model.at(row, 2) = row == 0 ? normal.x : row == 1 ? normal.y : normal.z;
      }
    }
    auto sizing = Matrix4::Identity();
    sizing.at(0,0) = sprite.size.x;
    sizing.at(1,1) = sprite.size.y;
    sizing.at(0,3) = (0.5f - sprite.frame.pivot.x) * sprite.size.x + sprite.offset.x;
    sizing.at(1,3) = -sprite.frame.pivot.y * sprite.size.y + sprite.offset.y;
    model = model * sizing;
    (void)Math::inverseAffine(model);
    result.instances.push_back({quad, model, Material{}, entity.id(), assets.share(sprite.frame.texture),
                                region, sprite.alphaCutoff, true});
  }
  return result;
}

std::vector<RenderLight> realtimeLights(const SceneSnapshot& scene) {
  auto lights = scene.lights;
  for (std::size_t instanceIndex=0;instanceIndex<scene.instances.size();++instanceIndex) {
    const auto& instance=scene.instances[instanceIndex];
    if (instance.unlit || instance.material.emission.maximum() <= 0) continue;
    Vec3 center{}, normal{};
    float area = 0;
    const auto& data = instance.mesh->data();
    for (std::size_t i=0; i<data.indices.size(); i+=3) {
      const auto a = Math::point(instance.objectToWorld, data.vertices[data.indices[i]].position);
      const auto b = Math::point(instance.objectToWorld, data.vertices[data.indices[i+1]].position);
      const auto c = Math::point(instance.objectToWorld, data.vertices[data.indices[i+2]].position);
      const auto n = cross(b-a,c-a);
      const float weight = Math::length(n)*.5f;
      center = center + (a+b+c)*(weight/3);
      normal = normal + n;
      area += weight;
    }
    if (area > 0) {
      RenderLight light;
      light.type = LightType::Area;
      light.sourceInstance=static_cast<int>(instanceIndex);
      light.position = center*(1/area);
      light.direction = Math::length(normal) > 1e-5f ? Math::unit(normal) : Vec3{0,-1,0};
      light.intensity = instance.material.emission;
      light.size = {std::sqrt(area), std::sqrt(area)};
      light.twoSided = instance.material.twoSidedEmission || Math::length(normal) < 1e-5f;
      if(Math::length(normal)<1e-5f) {
        // Closed emissive surfaces have no single facing direction. Approximate
        // their total Lambertian flux as an isotropic point source.
        light.type=LightType::Point;
        light.intensity=instance.material.emission*(area*.25f);
      }
      lights.push_back(light);
    }
  }
  return lights;
}
} // namespace Jade::Renderer
