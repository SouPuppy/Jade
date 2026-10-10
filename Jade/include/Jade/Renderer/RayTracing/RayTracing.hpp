#pragma once

#include <Jade/Renderer/RayTracing/ProgressiveBackend.hpp>
#include <Jade/Renderer/RayTracing/Integrator.hpp>

#include <memory>

namespace Jade::Renderer {

struct RayTracingConfig : ProgressiveSettings {
  std::uint32_t maxBounces = 12;
  std::uint32_t rrStartBounce = 4;
  std::uint32_t workerCount = 0;
};

class RayTracing final : public ProgressiveBackend {
public:
  explicit RayTracing(RayTracingConfig config = {});
  ~RayTracing();
  RayTracing(const RayTracing &) = delete;
  RayTracing &operator=(const RayTracing &) = delete;
  std::string_view name() const noexcept override { return "RayTracing"; }
  ProgressiveSettings samplingSettings() const override;
  void setIntegrator(std::unique_ptr<Integrator> integrator);
  void render(const RayScene &scene, const RenderRequest &request, Film &film) override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade::Renderer
