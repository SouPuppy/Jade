#include "GlfwWindow.h"

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace Jade::Detail {

GlfwWindow::GlfwWindow(const WindowConfig& config, WindowClientApi clientApi)
{
  if (config.width <= 0 || config.height <= 0) {
    throw std::invalid_argument("Window dimensions must be positive.");
  }

  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_VISIBLE, config.visible ? GLFW_TRUE : GLFW_FALSE);
  if (clientApi == WindowClientApi::OpenGL) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  } else {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  }

  m_Handle = glfwCreateWindow(config.width, config.height,
    config.title.c_str(), nullptr, nullptr);
  if (!m_Handle) {
    throw std::runtime_error("Failed to create the Jade window.");
  }
}

GlfwWindow::~GlfwWindow()
{
  glfwDestroyWindow(m_Handle);
}

} // namespace Jade::Detail
