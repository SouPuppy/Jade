#pragma once

#include <Jade/Color.hpp>

#include "../Platform/Window.hpp"
#include "OpenGL/OpenGLRenderer.hpp"

#include <memory>

namespace Jade {

class Line;
class Triangle;
class Object;
class Camera;

// Internal facade; the window must outlive its renderer.
class Renderer final {
public:
  Renderer(Window& window, bool vsync, float viewHeight = 10.0f);
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void setWireframe(bool enabled) noexcept;
  bool BeginFrame(FramebufferSize size, const Color& clearColor);
  void DrawLine(const Line& line);
  void DrawTriangle(const Triangle& triangle);
  void DrawObject(const Object& object, const Camera& camera);
  void DrawAxisGizmo(const Camera& camera);
  void Present();
  bool ValidateClear(const Color& expectedColor) const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

struct Renderer::Impl {
  Detail::OpenGLRenderer backend;

  Impl(void* nativeWindow, bool vsync, float viewHeight)
    : backend(static_cast<GLFWwindow*>(nativeWindow), vsync, viewHeight)
  {
  }
};

} // namespace Jade
