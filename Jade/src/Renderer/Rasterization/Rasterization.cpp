#include "Rasterization.hpp"
#include "RasterPipeline.hpp"
#include "../../internal/Renderer/RasterizationAccess.hpp"
#include <atomic>
#include <cmath>
namespace Jade::Renderer {
struct Rasterization::Impl {
  RasterizationConfig config;
  std::uint64_t id;
  Jade::Detail::RasterStatistics statistics;
};
Rasterization::Rasterization(RasterizationConfig config) {
  if(!std::isfinite(config.resolutionScale)||config.resolutionScale<=0||config.resolutionScale>8||!std::isfinite(config.exposureEV)||std::abs(config.exposureEV)>64 ||
     config.areaSamples<1||config.areaSamples>32||config.shadowResolution<16||config.shadowResolution>4096||
     config.reflectionProbeResolution<16||config.reflectionProbeResolution>2048||config.maxReflectionProbes>16||
     config.reflectionSteps<1||config.reflectionSteps>512||config.refractionSteps<1||config.refractionSteps>512)
    throw std::invalid_argument("Invalid rasterization configuration.");
  static std::atomic<std::uint64_t> next=1;
  m_Impl=std::make_unique<Impl>(Impl{config,next.fetch_add(1),{}});
}
Rasterization::~Rasterization()=default;
RenderSettings Rasterization::settings() const { return m_Impl->config; }
void Rasterization::render(const SceneSnapshot& scene,const FrameRequest& request,FrameTarget& target) {
  if(request.size!=target.size())throw std::invalid_argument("Frame request dimensions differ from target.");
  auto* gpu=dynamic_cast<Jade::Detail::OpenGLFrameTarget*>(&target);
  if(!gpu)throw std::invalid_argument("Rasterization requires an OpenGL frame target.");
  gpu->device.current();
  auto& pipeline=gpu->device.raster(m_Impl->id,m_Impl->config);
  pipeline.render(scene,*gpu);m_Impl->statistics=pipeline.statistics();
}
} // namespace Jade::Renderer
namespace Jade::Detail {
RasterStatistics RasterizationAccess::statistics(const Renderer::Rasterization& renderer) { return renderer.m_Impl->statistics; }
} // namespace Jade::Detail
