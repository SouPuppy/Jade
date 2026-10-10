#pragma once

#include <Jade/Renderer/Backend.hpp>
#include <Jade/Renderer/Integrator.hpp>

#include <memory>

namespace Jade::Renderer {

struct RayTracingConfig : RenderSettings {
  std::uint32_t maxBounces = 12;
  std::uint32_t rrStartBounce = 4;
  std::uint32_t workerCount = 0;
};

class RayTracing final : public Backend {
public:
  explicit RayTracing(RayTracingConfig config = {});
  ~RayTracing();
  RayTracing(const RayTracing &) = delete;
  RayTracing &operator=(const RayTracing &) = delete;
  RenderSettings settings() const override;
  void setIntegrator(std::unique_ptr<Integrator> integrator);
  void render(const RenderScene &scene, const RenderRequest &request, Film &film) override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade::Renderer
