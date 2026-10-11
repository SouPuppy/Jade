#include <Jade/Renderer/System.hpp>
#include "../internal/Renderer/Host/RenderHost.hpp"

namespace Jade::Renderer {
namespace {
Config validated(Config config) {
  switch(config.pipeline) {
    case RenderMode::Raster:case RenderMode::Hybrid:case RenderMode::RayTracing:case RenderMode::None:return config;
    default:throw std::invalid_argument("Invalid pipeline configuration.");
  }
}
}
struct System::Impl {
  bool wireframe;
  bool normalDebug;
  RenderMode mode;
  bool soul=false;
  Detail::RenderHost host;
  Impl(Window& window, JobSystem& jobs, Config config, std::string title)
    : wireframe(config.wireframe), normalDebug(config.normalDebug), mode(config.rayTracing.enabled ? config.pipeline : RenderMode::None),
      host(window, jobs, std::move(title), [&]{config.rayTracing.enabled=mode==RenderMode::RayTracing;return config;}()) {}
};
System::System(Window& window, JobSystem& jobs, Config config, std::string title)
  : m_Impl(std::make_unique<Impl>(window, jobs, validated(config), std::move(title))) {}
System::~System() = default;
RayTracing& System::rayTracing() noexcept { return m_Impl->host.rayTracing(); }
RayTracingSettings System::rayTracingSettings() const { return m_Impl->host.rayTracingSettings(); }
void System::setRayTracingSettings(const RayTracingSettings& settings) {
  const bool wasEnabled=m_Impl->host.rayTracingSettings().enabled;
  m_Impl->host.setRayTracingSettings(settings);
  if(wasEnabled!=settings.enabled) setPipeline(settings.enabled?RenderMode::RayTracing:RenderMode::None);
}
void System::setPipeline(RenderMode mode) {
  switch(mode) {
    case RenderMode::Raster:case RenderMode::Hybrid:case RenderMode::RayTracing:case RenderMode::None: break;
    default:throw std::invalid_argument("Invalid RenderMode.");
  }
  if(mode==m_Impl->mode) return;
  m_Impl->mode=mode;
  m_Impl->host.setRayTracingEnabled(mode==RenderMode::RayTracing);
  m_Impl->host.invalidate();
}
RenderMode System::pipeline() const noexcept {return m_Impl->mode;}
void System::nextPipeline() {
  switch(pipeline()) {
    case RenderMode::Raster:setPipeline(RenderMode::Hybrid);break;
    case RenderMode::Hybrid:setPipeline(RenderMode::RayTracing);break;
    case RenderMode::RayTracing:setPipeline(RenderMode::None);break;
    case RenderMode::None:setPipeline(RenderMode::Raster);break;
  }
}
bool System::sceneRenderingEnabled() const noexcept {return pipeline()!=RenderMode::None;}
RenderProgress System::progress() const {
  if(m_Impl->normalDebug) {
    RenderProgress result;
    result.rendererName = std::string("Normals") + (m_Impl->wireframe ? " + Wireframe" : "") + (m_Impl->soul ? " | Soul" : "");
    return result;
  }
  if(m_Impl->soul) {RenderProgress result;result.rendererName="Raster | Soul";return result;}
  auto result = rayTracingEnabled() ? m_Impl->host.progress() : RenderProgress{};
  if (rayTracingEnabled()) {
    if (m_Impl->wireframe) result.rendererName += " + Wireframe";
  } else result.rendererName = std::string(pipelineName(pipeline())) + (m_Impl->wireframe ? " + Wireframe" : "");
  return result;
}
bool System::rayTracingEnabled() const noexcept { return pipeline()==RenderMode::RayTracing; }
bool System::wireframeEnabled() const noexcept { return m_Impl->wireframe; }
void System::setRayTracingEnabled(bool enabled) noexcept {
  m_Impl->mode=enabled?RenderMode::RayTracing:RenderMode::None;
  m_Impl->host.setRayTracingEnabled(enabled);
}
void System::setWireframe(bool enabled) noexcept { m_Impl->wireframe = enabled; }
void System::setNormalDebug(bool enabled) noexcept { m_Impl->normalDebug = enabled; }
bool System::normalDebugEnabled() const noexcept { return m_Impl->normalDebug; }
void System::start() { m_Impl->host.start(rayTracingEnabled() && !normalDebugEnabled()); }
void System::stop() noexcept { m_Impl->host.stop(); }
void System::invalidate() noexcept { m_Impl->host.invalidate(); }
void System::rethrowError() { m_Impl->host.rethrowError(); }
void System::render(const SceneSnapshot* snapshot, ImageSize size, bool axes, const Detail::CollisionDebugLines* debug,
                    bool paused, std::chrono::steady_clock::time_point deadline, const CameraFrame* observer, const std::vector<PathSegment>* paths) {
  m_Impl->soul=observer!=nullptr;
  m_Impl->host.draw(snapshot, size, pipeline(), m_Impl->wireframe, axes, debug, paused, deadline, observer, paths, m_Impl->normalDebug);
}
} // namespace Jade::Renderer
