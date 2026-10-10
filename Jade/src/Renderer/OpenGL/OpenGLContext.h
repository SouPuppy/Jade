#pragma once

struct GLFWwindow;

namespace Jade::Detail {

class OpenGLContext final {
public:
  OpenGLContext(GLFWwindow* window, bool vsync);
  ~OpenGLContext();

  OpenGLContext(const OpenGLContext&) = delete;
  OpenGLContext& operator=(const OpenGLContext&) = delete;

  void MakeCurrent() const;
  void Present() const;

private:
  GLFWwindow* m_Window;
};

} // namespace Jade::Detail
