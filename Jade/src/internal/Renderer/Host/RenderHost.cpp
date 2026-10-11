#include "RenderHost.hpp"
#include "../../Platform/Window.hpp"

namespace Jade::Detail {
RenderHost::RenderHost(Window &window, JobSystem& jobs, bool vsync, std::string title, Renderer::RayTracingConfig config)
    : m_Window(window), m_Title(std::move(title)), m_Device(window, vsync), m_Display(m_Device),
      m_Debug(m_Device), m_RayTracing(jobs, config), m_Session(&jobs)
{
  m_Session.bind(m_RayTracing);
}
RenderHost::~RenderHost() { stop(); }
void RenderHost::start(bool render)
{
  stop();
  invalidate();
  m_Render = render;
  m_RayTracing.m_ApplicationActive = true;
  try {
    if (render)
      m_Session.start();
  } catch (...) {
    stop();
    throw;
  }
}
void RenderHost::stop() noexcept
{
  m_Session.stop();
  m_RayTracing.m_ApplicationActive = false;
}
void RenderHost::invalidate() noexcept { m_Session.invalidate(); }
void RenderHost::rethrowError() { m_Session.rethrowError(); }
Renderer::RenderProgress RenderHost::progress() const
{
  if (!m_Render) {
    Renderer::RenderProgress result;
    result.rendererName = "Disabled";
    return result;
  }
  auto result = m_Session.progress();
  result.rendererName = "RayTracing";
  return result;
}
void RenderHost::draw(const Renderer::SceneSnapshot *snapshot, Renderer::ImageSize size, bool render, bool wireframe,
                      bool axes, const CollisionDebugLines* colliders)
{
  if (m_Render != render) {
    m_Render = render;
    m_Session.stop();
    invalidate();
    rethrowError();
    if (render)
      m_Session.start();
  }
  if (m_FrameSize != size) {
    invalidate();
    m_FrameSize = size;
  }
  const auto name = render ? (wireframe ? "RayTracing + Wireframe" : "RayTracing") :
    (wireframe ? "Wireframe" : "Disabled");
  m_Window.SetTitle(m_Title + " | " + name);
  m_Display.begin(size);
  if (snapshot && snapshot->camera) {
    if (render) {
      m_Session.submit(*snapshot, size);
      if (auto image = m_Session.image())
        m_Display.image(*image);
    }
    if (wireframe)
      m_Debug.wireframe(*snapshot, size);
    if (axes)
      m_Debug.axes(*snapshot->camera, size);
    if (colliders)
      m_Debug.colliders(*colliders, *snapshot->camera, size);
  } else
    invalidate();
  m_Display.present();
}
} // namespace Jade::Detail
