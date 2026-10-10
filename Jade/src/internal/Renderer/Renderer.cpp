#include "Renderer.hpp"


namespace Jade {

Renderer::Renderer(Window& window, bool vsync)
  : m_Impl(std::make_unique<Impl>(window.NativeHandle(), vsync))
{
}

Renderer::~Renderer() = default;

void Renderer::setWireframe(bool enabled) noexcept
{
  m_Impl->backend.setWireframe(enabled);
}

bool Renderer::BeginFrame(FramebufferSize size, const Color& clearColor)
{
  return m_Impl->backend.BeginFrame(size, clearColor);
}

void Renderer::DrawMesh(const Mesh& mesh, const Detail::DrawParameters& parameters)
{
  m_Impl->backend.DrawMesh(mesh, parameters);
}

void Renderer::Present()
{
  m_Impl->backend.Present();
}

bool Renderer::ValidateClear(const Color& expectedColor) const
{
  return m_Impl->backend.ValidateClear(expectedColor);
}

} // namespace Jade
