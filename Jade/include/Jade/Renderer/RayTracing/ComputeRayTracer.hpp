#pragma once
#include <Jade/Renderer/RayTracing/ProgressiveBackend.hpp>
#include <Jade/Renderer/RayTracing/RayTracingSettings.hpp>
#include <memory>
#include <string_view>

namespace Jade::Renderer {
// OpenGL compute ray/path tracing. Headless render explicitly reads one sample;
// Runtime uses shared GPU accumulation/textures for direct presentation.
// Owns an OpenGL 4.3 compute context. Construct/destroy on the window thread;
// render may run on the progressive worker.
class ComputeRayTracer final : public ProgressiveBackend {
public:
  ComputeRayTracer();
  explicit ComputeRayTracer(RayTracingSettings config);
  ~ComputeRayTracer();
  ComputeRayTracer(const ComputeRayTracer &) = delete;
  ComputeRayTracer &operator=(const ComputeRayTracer &) = delete;
  std::string_view name() const noexcept override;
  ProgressiveSettings samplingSettings() const override;
  ImageEncoding imageEncoding() const noexcept override;
  std::string_view deviceName() const noexcept;
  void render(const RayScene &, const RenderRequest &, Film &) override;
private:
  friend class Jade::Detail::RenderHost;
  ComputeRayTracer(RayTracingSettings, bool shareWithCurrent);
  void configure(const RayTracingSettings&);
  bool hasGpuFrames() const noexcept override;
  std::shared_ptr<Jade::Detail::GpuFrame> renderGpuFrame(const RayScene&, const RenderRequest&) override;
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Jade::Renderer
