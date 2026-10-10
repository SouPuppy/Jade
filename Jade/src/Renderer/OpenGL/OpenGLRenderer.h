#pragma once

#include <Jade/Renderer/Renderer.h>

#include "OpenGLContext.h"

namespace Jade::Detail {

class OpenGLRenderer final {
public:
  OpenGLRenderer(GLFWwindow* window, bool vsync);

  bool BeginFrame(FramebufferSize size, const Color& clearColor);
  void Present();
  bool ValidateClear(const Color& expectedColor) const;

private:
  OpenGLContext m_Context;
  FramebufferSize m_Size;
};

} // namespace Jade::Detail
