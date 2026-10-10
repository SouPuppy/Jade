#pragma once

#include <Jade/Color.hpp>

#include "../Platform/Window.hpp"
#include "OpenGL/OpenGLRenderer.hpp"
#include "DrawParameters.hpp"

#include <memory>

namespace Jade {

// Internal facade; the window must outlive its renderer.
class Renderer final {
public:
  Renderer(Window& window, bool vsync);
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void setWireframe(bool enabled) noexcept;
  bool wireframeEnabled() const noexcept;
  bool BeginFrame(FramebufferSize size, const Color& clearColor);
  void DrawMesh(const Mesh& mesh, const Detail::DrawParameters& parameters);
  void Present();
  bool ValidateClear(const Color& expectedColor) const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

struct Renderer::Impl {
  Detail::OpenGLRenderer backend;

  Impl(void* nativeWindow, bool vsync)
    : backend(static_cast<GLFWwindow*>(nativeWindow), vsync)
  {
  }
};

} // namespace Jade
