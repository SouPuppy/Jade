#include "World.hpp"

namespace Untitled {
void buildWorld(Jade::Scene& scene, Jade::Assets& assets) {
  using namespace Jade;
  const auto cube = assets.add(MeshData::cube(1));
  auto box = [&](Vec3 position, Vec3 scale, Renderer::LinearRGB color, bool solid = true) {
    auto entity = scene.create<Archetype<Transform, Geometry, Material>>();
    entity.get<Transform>() = {position, {}, scale};
    entity.get<Geometry>().mesh = cube;
    entity.get<Material>() = Materials::Diffuse(color);
    if (solid) entity.add<Collider>(Box{});
    return entity;
  };
  box({0,-0.25f,0}, {24,0.5f,24}, {0.24f,0.32f,0.25f});
  // Low paving gives a visible world-space reference as the camera follows.
  for (int z = -2; z <= 2; ++z)
    for (int x = -2; x <= 2; ++x)
      box({x * 2.0f,-0.04f,z * 2.0f}, {1.92f,0.1f,1.92f},
        (x + z) % 2 ? Renderer::LinearRGB{0.39f,0.37f,0.30f} : Renderer::LinearRGB{0.48f,0.45f,0.36f}, false);
  for (int side : {-1,1}) {
    box({side * 12.0f,0.3f,0}, {0.5f,0.6f,24.5f}, {0.18f,0.24f,0.21f});
    box({0,0.3f,side * 12.0f}, {23.5f,0.6f,0.5f}, {0.18f,0.24f,0.21f});
  }
  box({-3,0.5f,-3}, {0.8f,1,0.8f}, {0.55f,0.23f,0.12f});
  box({3,0.75f,-3}, {0.8f,1.5f,0.8f}, {0.12f,0.35f,0.45f});
  box({-3,0.375f,3}, {0.8f,0.75f,0.8f}, {0.48f,0.40f,0.12f});
  box({3,0.6f,3}, {0.8f,1.2f,0.8f}, {0.29f,0.20f,0.43f});
  MeshData light;
  light.vertices = {{{-0.5f,0,-0.5f},{0,-1,0}}, {{0.5f,0,-0.5f},{0,-1,0}},
                    {{0.5f,0,0.5f},{0,-1,0}}, {{-0.5f,0,0.5f},{0,-1,0}}};
  light.indices = {0,1,2,0,2,3};
  auto lamp = scene.create<Archetype<Transform, Geometry, Material>>();
  // Above the whole orthographic frustum so the light cannot cover the view.
  lamp.get<Transform>() = {{-4,14,-2},{},{4,1,4}};
  lamp.get<Geometry>().mesh = assets.add(std::move(light));
  lamp.get<Material>() = Materials::Emissive({64,56,48});
  scene.physics().fields.clear();
}
} // namespace Untitled
