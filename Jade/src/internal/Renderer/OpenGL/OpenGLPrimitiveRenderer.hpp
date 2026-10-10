#pragma once

#include <Jade/Objects/Camera.hpp>
#include <Jade/Geometry/Line.hpp>
#include <Jade/Geometry/Triangle.hpp>

#include "../../Math/Matrix4.hpp"
#include "../../Platform/Window.hpp"
#include "OpenGLProgram.hpp"

#include <glad/gl.h>

namespace Jade::Detail {

// All GPU resources belong to the renderer and require its current context.
class OpenGLPrimitiveRenderer final {
public:
  OpenGLPrimitiveRenderer();
  ~OpenGLPrimitiveRenderer();

  OpenGLPrimitiveRenderer(const OpenGLPrimitiveRenderer&) = delete;
  OpenGLPrimitiveRenderer& operator=(const OpenGLPrimitiveRenderer&) = delete;

  void DrawLine(const Line& line, FramebufferSize size, float viewHeight);
  void DrawTriangle(const Triangle& triangle, FramebufferSize size, float viewHeight);
  void DrawAxisGizmo(const Camera& camera, FramebufferSize size);

private:
  void Draw(const float* vertices, GLsizei count, GLenum topology, const Matrix4& transform);
  void Release() noexcept;

  OpenGLProgram m_Program;
  GLuint m_VertexArray = 0;
  GLuint m_VertexBuffer = 0;
  GLint m_TransformLocation = -1;
};

} // namespace Jade::Detail
