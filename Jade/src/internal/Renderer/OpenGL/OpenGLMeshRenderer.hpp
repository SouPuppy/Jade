#pragma once

#include <Jade/Objects/Camera.hpp>
#include <Jade/Objects/Object.hpp>

#include "OpenGLProgram.hpp"
#include "../../Platform/Window.hpp"

#include <vector>

namespace Jade::Detail {

class OpenGLMeshRenderer final {
public:
  OpenGLMeshRenderer();
  ~OpenGLMeshRenderer();
  OpenGLMeshRenderer(const OpenGLMeshRenderer&) = delete;
  OpenGLMeshRenderer& operator=(const OpenGLMeshRenderer&) = delete;

  void DrawObject(const Object& object, const Camera& camera, FramebufferSize size);

private:
  void Release() noexcept;

  OpenGLProgram m_Program;
  GLuint m_VertexArray = 0;
  GLuint m_VertexBuffer = 0;
  GLuint m_IndexBuffer = 0;
  GLint m_TransformLocation = -1;
  GLint m_NormalLocation = -1;
  GLint m_ColorLocation = -1;
  std::vector<float> m_VertexData;
};

} // namespace Jade::Detail
