#include "Renderer.hpp"

#include <cmath>
#include <stdexcept>

namespace Jade {

Renderer::Renderer(Window& window, bool vsync, float viewHeight)
{
  if (!std::isfinite(viewHeight) || viewHeight <= 0.0f) {
    throw std::invalid_argument("View height must be positive and finite.");
  }
  m_Impl = std::make_unique<Impl>(window.NativeHandle(), vsync, viewHeight);
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

void Renderer::DrawLine(const Line& line)
{
  m_Impl->backend.DrawLine(line);
}

void Renderer::DrawTriangle(const Triangle& triangle)
{
  m_Impl->backend.DrawTriangle(triangle);
}

void Renderer::DrawObject(const Object& object, const Camera& camera)
{
  m_Impl->backend.DrawObject(object, camera);
}

void Renderer::DrawAxisGizmo(const Camera& camera)
{
  m_Impl->backend.DrawAxisGizmo(camera);
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
