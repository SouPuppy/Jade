#include <Jade/Renderer/System.hpp>
#include "../internal/Renderer/Host/RenderHost.hpp"

namespace Jade::Renderer {
struct System::Impl {
  bool render, wireframe;
  Detail::RenderHost host;
  Impl(Window& window, JobSystem& jobs, Config config, std::string title)
    : render(config.render), wireframe(config.wireframe),
      host(window, jobs, config.vsync, std::move(title), config.rayTracing) {}
};
System::System(Window& window, JobSystem& jobs, Config config, std::string title)
  : m_Impl(std::make_unique<Impl>(window, jobs, config, std::move(title))) {}
System::~System() = default;
RayTracing& System::rayTracing() noexcept { return m_Impl->host.rayTracing(); }
RenderProgress System::progress() const {
  auto result = m_Impl->render ? m_Impl->host.progress() : RenderProgress{};
  result.rendererName = m_Impl->render ? (m_Impl->wireframe ? "RayTracing + Wireframe" : "RayTracing") :
    (m_Impl->wireframe ? "Wireframe" : "Disabled");
  return result;
}
bool System::renderEnabled() const noexcept { return m_Impl->render; }
bool System::wireframeEnabled() const noexcept { return m_Impl->wireframe; }
void System::setRender(bool enabled) noexcept {
  if (enabled != m_Impl->render) { m_Impl->render = enabled; m_Impl->host.invalidate(); }
}
void System::setWireframe(bool enabled) noexcept { m_Impl->wireframe = enabled; }
void System::start() { m_Impl->host.start(m_Impl->render); }
void System::stop() noexcept { m_Impl->host.stop(); }
void System::invalidate() noexcept { m_Impl->host.invalidate(); }
void System::rethrowError() { m_Impl->host.rethrowError(); }
void System::render(const SceneSnapshot* snapshot, ImageSize size, bool axes, const Detail::CollisionDebugLines* debug) {
  m_Impl->host.draw(snapshot, size, m_Impl->render, m_Impl->wireframe, axes, debug);
}
} // namespace Jade::Renderer
