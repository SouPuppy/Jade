#pragma once
#include "OpenGLContext.hpp"
#include <glad/gl.h>
#include <thread>
namespace Jade {
class Window;
}
namespace Jade::Detail {
class OpenGLDevice final {
public:
  OpenGLDevice(Window &, bool vsync);
  void current() const;
  void present() const;

private:
  std::thread::id m_Owner = std::this_thread::get_id();
  OpenGLContext m_Context;
};
} // namespace Jade::Detail
