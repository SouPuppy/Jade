#pragma once
#include <Jade/Assets/Assets.hpp>
#include <Jade/Core/Scene.hpp>
#include <Jade/Materials/Material.hpp>
#include <Jade/Renderer/Common/CameraFrame.hpp>
#include <Jade/Animation/SpriteFrame.hpp>
#include <Jade/Graphics/Light.hpp>
#include <memory>
#include <optional>
#include <vector>
namespace Jade::Renderer {
struct RenderInstance {
  std::shared_ptr<const Mesh> mesh;
  Matrix4 objectToWorld = Matrix4::Identity();
  Material material;
  std::uint64_t entity = 0;
  // Sprite quads use local x + 0.5, 1 - y as top-left UV coordinates.
  std::shared_ptr<const Texture> texture;
  SpriteRegion region;
  float alphaCutoff = 0.5f;
  bool unlit = false;
  bool operator==(const RenderInstance &other) const
  {
    return mesh == other.mesh && objectToWorld.values == other.objectToWorld.values &&
           material == other.material && entity == other.entity && texture == other.texture &&
           region == other.region && alphaCutoff == other.alphaCutoff && unlit == other.unlit;
  }
};

struct RenderLight {
  LightType type = LightType::Point;
  Vec3 position, direction{0,0,-1}, right{1,0,0}, up{0,1,0};
  LinearRGB intensity;
  Vec2 size{1,1};
  bool twoSided = false;
  int sourceInstance = -1; // Realtime emissive-mesh approximation only.
  bool operator==(const RenderLight&) const = default;
};
std::vector<RenderLight> realtimeLights(const struct SceneSnapshot&);
// Capture only on the owning thread; retained immutable meshes outlive Assets.
struct SceneSnapshot {
  std::vector<RenderInstance> instances;
  std::optional<CameraFrame> camera;
  std::vector<RenderLight> lights;
  std::vector<Matrix4> entityTransforms;
  std::vector<CameraFrame> cameras;
  bool operator==(const SceneSnapshot& other) const {
    return instances==other.instances && camera==other.camera && lights==other.lights;
  }
  // Camera-only capture lets debug overlays run while scene rendering is disabled.
  static SceneSnapshot capture(const Scene &scene, const Assets &assets, bool includeGeometry = true, bool includeDebug = false);
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
