#pragma once

#include <Jade/Assets/Assets.hpp>
#include <Jade/Core/Scene.hpp>
#include <Jade/Graphics/Material.hpp>
#include <Jade/Renderer/CameraFrame.hpp>

#include <memory>
#include <optional>
#include <stop_token>
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
  static SceneSnapshot capture(const Scene &scene, const Assets &assets);
};

struct EmitterSample {
  Vec3 position;
  Vec3 normal;
  LinearRGB emission;
  float areaPdf = 0;
  Vec3 positionError;
  bool twoSided = false;
};

// Detached immutable scene. Calls are safe concurrently after construction.
class RenderScene final : public RayQuery {
public:
  RenderScene();
  static RenderScene build(const SceneSnapshot &snapshot);
  bool intersect(const Ray &ray, SurfaceHit &hit) const override;
  bool occluded(const Ray &ray) const override;
  const Material &material(const SurfaceHit &hit) const;
  EmitterSample sampleEmitter(float selection, Vec2 sample) const;
  float emitterPdf(Vec3 reference, const SurfaceHit &hit) const;
  std::size_t instanceCount() const noexcept;
  std::size_t emitterCount() const noexcept;
  std::size_t meshCount() const noexcept;

private:
  friend class SceneCompiler;
  struct Impl;
  std::shared_ptr<const Impl> m_Impl;
};

// Reuses mesh BVHs across scene revisions and rebuilds only changed geometry.
class SceneCompiler final {
public:
  SceneCompiler();
  ~SceneCompiler();
  SceneCompiler(const SceneCompiler &) = delete;
  SceneCompiler &operator=(const SceneCompiler &) = delete;
  // Cancellation returns an empty scene; completed cached mesh BVHs remain reusable.
  RenderScene compile(const SceneSnapshot &snapshot, std::stop_token cancellation = {});

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade::Renderer
