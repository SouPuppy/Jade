#pragma once

#include <Jade/Renderer/RayTracing/ProgressiveBackend.hpp>
#include <Jade/Renderer/RayTracing/RayTracingSettings.hpp>
#include <Jade/Renderer/RayTracing/Integrator.hpp>

#include <memory>

namespace Jade { class JobSystem; }

namespace Jade::Renderer {

class RayTracing final : public ProgressiveBackend {
public:
  explicit RayTracing(RayTracingSettings config = {});
  RayTracing(JobSystem& jobs, RayTracingSettings config = {});
  ~RayTracing();
  RayTracing(const RayTracing &) = delete;
  RayTracing &operator=(const RayTracing &) = delete;
  std::string_view name() const noexcept override { return "RayTracing"; }
  ProgressiveSettings samplingSettings() const override;
  ImageEncoding imageEncoding() const noexcept override;
  void setIntegrator(std::unique_ptr<Integrator> integrator);
  void render(const RayScene &scene, const RenderRequest &request, Film &film) override;

private:
  friend class Jade::Detail::RenderHost;
  void configure(const RayTracingSettings&);
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade::Renderer
