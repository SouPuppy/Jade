#pragma once
#include <Jade/Renderer/RenderScene.hpp>
#include "../Acceleration/BVH.hpp"
namespace Jade::Detail {
enum class SceneAccelerationUpdate { Reused, Refit, Rebuilt };
struct RenderSceneAccess {
  static const void *geometryIdentity(const Renderer::RenderScene &scene);
  static const void *emitterIdentity(const Renderer::RenderScene &scene);
  static SceneAccelerationUpdate update(const Renderer::RenderScene &scene);
  static BVHStats statistics(const Renderer::RenderScene &scene);
};
} // namespace Jade::Detail
