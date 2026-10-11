#include <Jade/Renderer/System.hpp>
#include "../internal/Renderer/Host/RenderHost.hpp"

namespace Jade::Renderer {
struct System::Impl {
  Mode mode;
  Detail::RenderHost host;
  Impl(Window& window, Config config, std::string title)
    : mode(config.mode), host(window, config.vsync, std::move(title), config.rayTracing) {}
};
System::System(Window& window, Config config, std::string title)
  : m_Impl(std::make_unique<Impl>(window, config, std::move(title))) {}
System::~System() = default;
RayTracing& System::rayTracing() noexcept { return m_Impl->host.rayTracing(); }
RenderProgress System::progress() const {
  if (m_Impl->mode == Mode::Wireframe) { RenderProgress p; p.rendererName = "Wireframe"; return p; }
  return m_Impl->host.progress();
}
Mode System::mode() const noexcept { return m_Impl->mode; }
void System::setMode(Mode mode) noexcept {
  if (mode != m_Impl->mode) { m_Impl->mode = mode; m_Impl->host.invalidate(); }
}
void System::start() { m_Impl->host.start(m_Impl->mode == Mode::Wireframe); }
void System::stop() noexcept { m_Impl->host.stop(); }
void System::invalidate() noexcept { m_Impl->host.invalidate(); }
void System::rethrowError() { m_Impl->host.rethrowError(); }
void System::render(const SceneSnapshot* snapshot, ImageSize size, bool axes, const Detail::CollisionDebugLines* debug) {
  m_Impl->host.draw(snapshot, size, m_Impl->mode == Mode::Wireframe, axes, debug);
}
} // namespace Jade::Renderer
