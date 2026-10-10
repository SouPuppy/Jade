#include "OpenGLDevice.hpp"
#include "../../../Platform/Window.hpp"
#include <stdexcept>
namespace Jade::Detail {
OpenGLDevice::OpenGLDevice(Window &window, bool vsync)
    : m_Context(static_cast<GLFWwindow *>(window.NativeHandle()), vsync)
{
}
void OpenGLDevice::current() const
{
  if (std::this_thread::get_id() != m_Owner)
    throw std::logic_error("OpenGL device used from a foreign thread.");
  m_Context.MakeCurrent();
}
void OpenGLDevice::present() const
{
  current();
  m_Context.Present();
}
} // namespace Jade::Detail
