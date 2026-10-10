#pragma once
#include <Jade/Graphics/Mesh.hpp>
#include "../Acceleration/BVH.hpp"
namespace Jade::Detail {
// Private bridge to immutable mesh acceleration.
struct MeshAccess {
  static const BVH &acceleration(const Mesh &mesh);
  static bool intersect(const Mesh &mesh, const Renderer::Ray &ray, Renderer::SurfaceHit &hit);
  static bool occluded(const Mesh &mesh, const Renderer::Ray &ray);
  static const void *identity(const Mesh &mesh);
};
} // namespace Jade::Detail
