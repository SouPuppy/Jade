#pragma once

#include <Jade/Renderer/Common/SceneSnapshot.hpp>
#include <Jade/Core/Scene.hpp>
#include <Jade/Materials/Material.hpp>
#include <Jade/Renderer/Common/CameraFrame.hpp>

#include <memory>
#include <optional>
#include <stop_token>
#include <vector>
#include <span>

namespace Jade::Detail {
struct RaySceneAccess;
}

namespace Jade::Renderer {

// Detached immutable scene. Calls are safe concurrently after construction.
class RayScene final : public RayQuery {
public:
  RayScene();
  static RayScene build(const SceneSnapshot &snapshot);
  bool intersect(const Ray &ray, SurfaceHit &hit) const override;
  bool occluded(const Ray &ray) const override;
  bool occluded(const Ray&, std::uint32_t ignoredInstance) const;
  const Material &material(const SurfaceHit &hit) const;
  bool unlit(const SurfaceHit& hit) const;
  Texel spriteSample(const SurfaceHit& hit) const;
  std::span<const RenderLight> lights() const noexcept;
  EmitterSample sampleEmitter(float selection, Vec2 sample) const;
  float emitterPdf(Vec3 reference, const SurfaceHit &hit) const;
  std::size_t instanceCount() const noexcept;
  std::size_t emitterCount() const noexcept;
  std::size_t meshCount() const noexcept;

private:
  friend class RaySceneCompiler;
  friend struct Jade::Detail::RaySceneAccess;
  struct Impl;
  std::shared_ptr<const Impl> m_Impl;
};

// Consumes precompiled meshes. Reuses, refits or SAH-rebuilds immutable instance TLAS.
class RaySceneCompiler final {
public:
  RaySceneCompiler();
  ~RaySceneCompiler();
  RaySceneCompiler(const RaySceneCompiler &) = delete;
  RaySceneCompiler &operator=(const RaySceneCompiler &) = delete;
  // Cancellation returns an empty scene without changing the last successful compilation.
  RayScene compile(const SceneSnapshot &snapshot, std::stop_token cancellation = {});

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade::Renderer
