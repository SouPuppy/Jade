#include <Jade/Platform/Window.h>

#include "GLFW/GlfwRuntime.h"
#include "GLFW/GlfwWindow.h"

#include <GLFW/glfw3.h>

namespace Jade {

struct Window::Impl {
  Detail::GlfwRuntime runtime;
  Detail::GlfwWindow window;

  explicit Impl(const WindowConfig& config)
    : window(config, Detail::WindowClientApi::OpenGL)
  {
  }
};

Window::Window(const WindowConfig& config)
  : m_Impl(std::make_unique<Impl>(config))
{
}

Window::~Window() = default;

void Window::PollEvents()
{
  glfwPollEvents();
}

void Window::WaitEvents(double timeoutSeconds)
{
  glfwWaitEventsTimeout(timeoutSeconds);
}

bool Window::ShouldClose() const
{
  return glfwWindowShouldClose(m_Impl->window.Handle()) == GLFW_TRUE;
}

void Window::RequestClose()
{
  glfwSetWindowShouldClose(m_Impl->window.Handle(), GLFW_TRUE);
}

bool Window::IsKeyPressed(Key key) const
{
  if (key == Key::Escape) {
    return glfwGetKey(m_Impl->window.Handle(), GLFW_KEY_ESCAPE) == GLFW_PRESS;
  }
  return false;
}

FramebufferSize Window::GetFramebufferSize() const
{
  FramebufferSize size;
  glfwGetFramebufferSize(m_Impl->window.Handle(), &size.width, &size.height);
  return size;
}

void* Window::NativeHandle() const
{
  return m_Impl->window.Handle();
}

} // namespace Jade
