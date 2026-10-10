#include <Jade/Renderer/Renderer.h>

#include "OpenGL/OpenGLRenderer.h"

namespace Jade {

struct Renderer::Impl {
  Detail::OpenGLRenderer backend;

  Impl(void* nativeWindow, bool vsync)
    : backend(static_cast<GLFWwindow*>(nativeWindow), vsync)
  {
  }
};

Renderer::Renderer(Window& window, bool vsync)
  : m_Impl(std::make_unique<Impl>(window.NativeHandle(), vsync))
{
}

Renderer::~Renderer() = default;

bool Renderer::BeginFrame(FramebufferSize size, const Color& clearColor)
{
  return m_Impl->backend.BeginFrame(size, clearColor);
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
