#include "OpenGLContext.hpp"

#include <Jade/Log.hpp>
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <string>

namespace Jade::Detail {

OpenGLContext::OpenGLContext(GLFWwindow* window, bool vsync)
  try
  : m_Window(window)
{
  MakeCurrent();
  if (!gladLoadGL(glfwGetProcAddress) || !GLAD_GL_VERSION_3_3) {
    throw std::runtime_error("Failed to load OpenGL 3.3 Core with GLAD.");
  }
  if (!glViewport || !glClearColor || !glClear || !glReadPixels ||
    !glReadBuffer || !glGetError || !glGetString) {
    throw std::runtime_error("Required OpenGL entry points are unavailable.");
  }

  glfwSwapInterval(vsync ? 1 : 0);
  Log::Info("OpenGL: " + std::string(
    reinterpret_cast<const char*>(glGetString(GL_VERSION))));
  Log::Info("Renderer: " + std::string(
    reinterpret_cast<const char*>(glGetString(GL_RENDERER))));
}
catch (...) {
  // A throwing constructor has no destructor. Release the binding even if
  // the caller keeps its Window alive after renderer initialization fails.
  if (glfwGetCurrentContext() == window) {
    glfwMakeContextCurrent(nullptr);
  }
  throw;
}

OpenGLContext::~OpenGLContext()
{
  if (glfwGetCurrentContext() == m_Window) {
    glfwMakeContextCurrent(nullptr);
  }
}

void OpenGLContext::MakeCurrent() const
{
  glfwMakeContextCurrent(m_Window);
}

void OpenGLContext::Present() const
{
  glfwSwapBuffers(m_Window);
}

} // namespace Jade::Detail
