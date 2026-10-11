#pragma once

#include <Jade/Renderer/RayTracing/RayScene.hpp>
#include <Jade/Renderer/RayTracing/Sampler.hpp>

namespace Jade::Renderer {

enum class PathSegmentKind { Primary, Bounce, ShadowVisible, ShadowBlocked, Escape };
struct PathSegment {
  Vec3 start,end;
  PathSegmentKind kind=PathSegmentKind::Primary;
};
class Integrator {
public:
  virtual ~Integrator() = default;
  virtual LinearRGB Li(const Ray &ray, const RayScene &scene, Sampler &sampler) const = 0;
};

struct PathIntegratorConfig {
  std::uint32_t maxBounces = 12;
  std::uint32_t rrStartBounce = 4;
};

class PathIntegrator final : public Integrator {
public:
  explicit PathIntegrator(PathIntegratorConfig config = {});
  LinearRGB Li(const Ray &ray, const RayScene &scene, Sampler &sampler) const override;
  LinearRGB record(const Ray&, const RayScene&, Sampler&, std::vector<PathSegment>&) const;

private:
  LinearRGB evaluate(const Ray&, const RayScene&, Sampler&, std::vector<PathSegment>*) const;
  PathIntegratorConfig m_Config;
};

} // namespace Jade::Renderer
