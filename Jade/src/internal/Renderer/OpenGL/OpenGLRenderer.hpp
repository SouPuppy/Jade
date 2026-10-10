#pragma once

#include <Jade/Color.hpp>
#include <Jade/Graphics/Mesh.hpp>
#include "../DrawParameters.hpp"

#include "../../Platform/Window.hpp"
#include "OpenGLContext.hpp"

#include <memory>

namespace Jade::Detail {

class OpenGLMeshRenderer;

class OpenGLRenderer final {
public:
  OpenGLRenderer(GLFWwindow* window, bool vsync);
  ~OpenGLRenderer();

  void setWireframe(bool enabled) noexcept { m_Wireframe = enabled; }
  bool wireframeEnabled() const noexcept { return m_Wireframe; }
  bool BeginFrame(FramebufferSize size, const Color& clearColor);
  void DrawMesh(const Mesh& mesh, const DrawParameters& parameters);
  void Present();
  bool ValidateClear(const Color& expectedColor) const;

private:
  OpenGLContext m_Context;
  std::unique_ptr<OpenGLMeshRenderer> m_Meshes;
  FramebufferSize m_Size;
  bool m_Wireframe = false;
};

} // namespace Jade::Detail
