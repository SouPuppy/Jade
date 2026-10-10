#pragma once

#include <Jade/Math/Vector.hpp>

#include <cstdint>
#include <limits>

namespace Jade::Renderer {

struct Ray {
  Vec3 origin;
  Vec3 direction{0, 0, -1};
  float tMin = 0;
  float tMax = std::numeric_limits<float>::infinity();
  Vec3 at(float t) const noexcept { return origin + direction * t; }
};

struct SurfaceHit {
  float t = std::numeric_limits<float>::infinity();
  Vec3 position;
  Vec3 geometricNormal;
  Vec3 shadingNormal;
  Vec3 positionError;
  Vec3 barycentric;
  std::uint32_t instance = 0;
  std::uint32_t primitive = 0;
  // Scene-local numeric identity; worker hits never retain live ECS handles.
  std::uint64_t entity = 0;
  bool frontFace = true;

  Ray spawn(Vec3 direction) const;
};

class RayQuery {
public:
  virtual ~RayQuery() = default;
  virtual bool intersect(const Ray &ray, SurfaceHit &hit) const = 0;
  virtual bool occluded(const Ray &ray) const = 0;
};

} // namespace Jade::Renderer
