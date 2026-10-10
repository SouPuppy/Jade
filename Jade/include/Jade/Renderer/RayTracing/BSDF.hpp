#pragma once

#include <Jade/Graphics/Material.hpp>
#include <Jade/Renderer/RayTracing/Ray.hpp>
#include <Jade/Renderer/RayTracing/Sampler.hpp>

namespace Jade::Renderer {

struct BSDFSample {
  Vec3 direction;
  LinearRGB value;
  float pdf = 0;
  bool delta = false;
  bool transmission = false;
  // Transmitted index / incident index; used to compensate roulette.
  float eta = 1;
};

class BSDF final {
public:
  BSDF(const Material &material, const SurfaceHit &hit);
  LinearRGB evaluate(Vec3 outgoing, Vec3 incoming) const;
  float pdf(Vec3 outgoing, Vec3 incoming) const;
  BSDFSample sample(Vec3 outgoing, Sampler &sampler) const;
  bool delta() const noexcept;

private:
  Material m_Material;
  Vec3 m_Normal, m_Geometric, m_Tangent, m_Bitangent;
  bool m_FrontFace;
  Vec3 local(Vec3 direction) const;
  Vec3 world(Vec3 direction) const;
};

} // namespace Jade::Renderer
