#pragma once
#include <Jade/Renderer/RayTracing/RayScene.hpp>
#include "../Acceleration/BVH.hpp"
namespace Jade::Detail {
enum class SceneAccelerationUpdate { Reused, Refit, Rebuilt };
struct RaySceneAccess {
  static const void *geometryIdentity(const Renderer::RayScene &scene);
  static const void *emitterIdentity(const Renderer::RayScene &scene);
  static SceneAccelerationUpdate update(const Renderer::RayScene &scene);
  static BVHStats statistics(const Renderer::RayScene &scene);
};
} // namespace Jade::Detail
