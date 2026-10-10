#include "OpenGLRenderer.h"

#include <glad/gl.h>

#include <array>
#include <cmath>

namespace Jade::Detail {

OpenGLRenderer::OpenGLRenderer(GLFWwindow* window, bool vsync)
  : m_Context(window, vsync)
{
}

bool OpenGLRenderer::BeginFrame(FramebufferSize size, const Color& clearColor)
{
  m_Size = size;
  if (size.width <= 0 || size.height <= 0) {
    return false;
  }

  m_Context.MakeCurrent();
  glViewport(0, 0, size.width, size.height);
  glClearColor(clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha);
  glClear(GL_COLOR_BUFFER_BIT);
  return true;
}

void OpenGLRenderer::Present()
{
  m_Context.Present();
}

bool OpenGLRenderer::ValidateClear(const Color& expectedColor) const
{
  if (m_Size.width <= 0 || m_Size.height <= 0) {
    return false;
  }

  m_Context.MakeCurrent();
  std::array<unsigned char, 4> pixel{};
  glReadBuffer(GL_BACK);
  glReadPixels(m_Size.width / 2, m_Size.height / 2, 1, 1,
    GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
  if (glGetError() != GL_NO_ERROR) {
    return false;
  }

  // Alpha precision varies with the window's pixel format; verify RGB.
  const std::array<float, 3> expected{
    expectedColor.red, expectedColor.green, expectedColor.blue
  };
  for (std::size_t channel = 0; channel < expected.size(); ++channel) {
    if (std::abs(static_cast<float>(pixel[channel]) / 255.0f -
        expected[channel]) > 2.0f / 255.0f) {
      return false;
    }
  }
  return true;
}

} // namespace Jade::Detail
