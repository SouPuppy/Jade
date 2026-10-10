#include "Window.hpp"
#include "GLFW/GlfwInput.hpp"

#include <GLFW/glfw3.h>

namespace Jade {

Window::Window(const WindowConfig& config)
  : m_Impl(std::make_unique<Impl>(config))
{
  Detail::InstallInputCallbacks(m_Impl->window.Handle(), m_Impl->input);
}

Window::~Window() = default;

void Window::PollEvents()
{
  glfwPollEvents();
  m_Impl->input.RethrowError();
}

void Window::WaitEvents(double timeoutSeconds)
{
  glfwWaitEventsTimeout(timeoutSeconds);
  m_Impl->input.RethrowError();
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
  return m_Impl->input.IsDown(key);
}

Detail::InputState& Window::Input() noexcept
{
  return m_Impl->input;
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
