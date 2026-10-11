#include <Jade/Renderer/System.hpp>
#include "../internal/Renderer/Host/RenderHost.hpp"

namespace Jade::Renderer {
struct System::Impl {
  bool wireframe;
  Detail::RenderHost host;
  Impl(Window& window, JobSystem& jobs, Config config, std::string title)
    : wireframe(config.wireframe),
      host(window, jobs, std::move(title), config) {}
};
System::System(Window& window, JobSystem& jobs, Config config, std::string title)
  : m_Impl(std::make_unique<Impl>(window, jobs, config, std::move(title))) {}
System::~System() = default;
RayTracing& System::rayTracing() noexcept { return m_Impl->host.rayTracing(); }
RayTracingSettings System::rayTracingSettings() const { return m_Impl->host.rayTracingSettings(); }
void System::setRayTracingSettings(const RayTracingSettings& settings) { m_Impl->host.setRayTracingSettings(settings); }
RenderProgress System::progress() const {
  auto result = rayTracingEnabled() ? m_Impl->host.progress() : RenderProgress{};
  if (rayTracingEnabled()) {
    if (m_Impl->wireframe) result.rendererName += " + Wireframe";
  } else result.rendererName = m_Impl->wireframe ? "Wireframe" : "Disabled";
  return result;
}
bool System::rayTracingEnabled() const noexcept { return m_Impl->host.rayTracingSettings().enabled; }
bool System::wireframeEnabled() const noexcept { return m_Impl->wireframe; }
void System::setRayTracingEnabled(bool enabled) noexcept {
  m_Impl->host.setRayTracingEnabled(enabled);
}
void System::setWireframe(bool enabled) noexcept { m_Impl->wireframe = enabled; }
void System::start() { m_Impl->host.start(rayTracingEnabled()); }
void System::stop() noexcept { m_Impl->host.stop(); }
void System::invalidate() noexcept { m_Impl->host.invalidate(); }
void System::rethrowError() { m_Impl->host.rethrowError(); }
void System::render(const SceneSnapshot* snapshot, ImageSize size, bool axes, const Detail::CollisionDebugLines* debug,
                    bool paused, std::chrono::steady_clock::time_point deadline) {
  m_Impl->host.draw(snapshot, size, rayTracingEnabled(), m_Impl->wireframe, axes, debug, paused, deadline);
}
} // namespace Jade::Renderer
