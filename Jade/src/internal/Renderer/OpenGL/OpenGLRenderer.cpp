#include "OpenGLRenderer.hpp"
#include "OpenGLPrimitiveRenderer.hpp"
#include "OpenGLMeshRenderer.hpp"

#include <glad/gl.h>

#include <array>
#include <cmath>

namespace Jade::Detail {

OpenGLRenderer::OpenGLRenderer(GLFWwindow* window, bool vsync, float viewHeight)
  : m_Context(window, vsync),
    m_Primitives(std::make_unique<OpenGLPrimitiveRenderer>()),
    m_Meshes(std::make_unique<OpenGLMeshRenderer>()),
    m_ViewHeight(viewHeight)
{
}

OpenGLRenderer::~OpenGLRenderer()
{
  m_Context.MakeCurrent();
  m_Meshes.reset();
  m_Primitives.reset();
}

bool OpenGLRenderer::BeginFrame(FramebufferSize size, const Color& clearColor)
{
  m_Size = size;
  if (size.width <= 0 || size.height <= 0) {
    return false;
  }

  m_Context.MakeCurrent();
  glViewport(0, 0, size.width, size.height);
  glPolygonMode(GL_FRONT_AND_BACK, m_Wireframe ? GL_LINE : GL_FILL);
  glClearColor(clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha);
  glDepthMask(GL_TRUE);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  return true;
}

void OpenGLRenderer::DrawLine(const Line& line)
{
  m_Context.MakeCurrent();
  m_Primitives->DrawLine(line, m_Size, m_ViewHeight);
}

void OpenGLRenderer::DrawTriangle(const Triangle& triangle)
{
  m_Context.MakeCurrent();
  m_Primitives->DrawTriangle(triangle, m_Size, m_ViewHeight);
}

void OpenGLRenderer::DrawObject(const Object& object, const Camera& camera)
{
  m_Context.MakeCurrent();
  m_Meshes->DrawObject(object, camera, m_Size);
}

void OpenGLRenderer::DrawAxisGizmo(const Camera& camera)
{
  m_Context.MakeCurrent();
  m_Primitives->DrawAxisGizmo(camera, m_Size);
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
