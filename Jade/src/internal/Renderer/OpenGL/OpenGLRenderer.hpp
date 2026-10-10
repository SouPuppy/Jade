#pragma once

#include <Jade/Objects/Camera.hpp>
#include <Jade/Color.hpp>
#include <Jade/Objects/Object.hpp>
#include <Jade/Geometry/Line.hpp>
#include <Jade/Geometry/Triangle.hpp>

#include "../../Platform/Window.hpp"
#include "OpenGLContext.hpp"

#include <memory>

namespace Jade::Detail {

class OpenGLPrimitiveRenderer;
class OpenGLMeshRenderer;

class OpenGLRenderer final {
public:
  OpenGLRenderer(GLFWwindow* window, bool vsync, float viewHeight);
  ~OpenGLRenderer();

  void setWireframe(bool enabled) noexcept { m_Wireframe = enabled; }
  bool BeginFrame(FramebufferSize size, const Color& clearColor);
  void DrawLine(const Line& line);
  void DrawTriangle(const Triangle& triangle);
  void DrawObject(const Object& object, const Camera& camera);
  void DrawAxisGizmo(const Camera& camera);
  void Present();
  bool ValidateClear(const Color& expectedColor) const;

private:
  OpenGLContext m_Context;
  std::unique_ptr<OpenGLPrimitiveRenderer> m_Primitives;
  std::unique_ptr<OpenGLMeshRenderer> m_Meshes;
  FramebufferSize m_Size;
  float m_ViewHeight;
  bool m_Wireframe = false;
};

} // namespace Jade::Detail
