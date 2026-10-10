#pragma once

#include <Jade/Renderer/RayTracing/RayScene.hpp>
#include <Jade/Renderer/RayTracing/Sampler.hpp>

namespace Jade::Renderer {

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

private:
  PathIntegratorConfig m_Config;
};

} // namespace Jade::Renderer
