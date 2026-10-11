#pragma once
#include <Jade/Assets/Assets.hpp>
#include <Jade/Core/Scene.hpp>
#include <Jade/Materials/Material.hpp>
#include <Jade/Renderer/Common/CameraFrame.hpp>
#include <memory>
#include <optional>
#include <vector>
namespace Jade::Renderer {
struct RenderInstance {
  std::shared_ptr<const Mesh> mesh;
  Matrix4 objectToWorld = Matrix4::Identity();
  Material material;
  std::uint64_t entity = 0;
  bool operator==(const RenderInstance &other) const
  {
    return mesh == other.mesh && objectToWorld.values == other.objectToWorld.values &&
           material == other.material && entity == other.entity;
  }
};

// Capture only on the owning thread; retained immutable meshes outlive Assets.
struct SceneSnapshot {
  std::vector<RenderInstance> instances;
  std::optional<CameraFrame> camera;
  bool operator==(const SceneSnapshot &) const = default;
  // Camera-only capture lets debug overlays run while scene rendering is disabled.
  static SceneSnapshot capture(const Scene &scene, const Assets &assets, bool includeGeometry = true);
};

struct EmitterSample {
  Vec3 position;
  Vec3 normal;
  LinearRGB emission;
  float areaPdf = 0;
  Vec3 positionError;
  bool twoSided = false;
};

} // namespace Jade::Renderer
