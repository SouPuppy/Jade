#pragma once

#include <glad/gl.h>

namespace Jade::Detail {

// Context-owned program shared by the primitive and mesh renderer implementations.
class OpenGLProgram final {
public:
  OpenGLProgram(const char* vertexSource, const char* fragmentSource);
  ~OpenGLProgram();
  OpenGLProgram(const OpenGLProgram&) = delete;
  OpenGLProgram& operator=(const OpenGLProgram&) = delete;

  GLuint Handle() const noexcept { return m_Handle; }
  GLint Uniform(const char* name) const;

private:
  GLuint m_Handle = 0;
};

} // namespace Jade::Detail
