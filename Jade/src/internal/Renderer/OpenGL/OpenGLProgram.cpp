#include "OpenGLProgram.hpp"

#include <stdexcept>
#include <string>

namespace Jade::Detail {
namespace {

class Shader final {
public:
  Shader(GLenum type, const char* source) : m_Handle(glCreateShader(type))
  {
    if (m_Handle == 0) {
      throw std::runtime_error("Failed to create an OpenGL shader.");
    }
    glShaderSource(m_Handle, 1, &source, nullptr);
    glCompileShader(m_Handle);
    GLint compiled = GL_FALSE;
    glGetShaderiv(m_Handle, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
      try {
        GLint length = 0;
        glGetShaderiv(m_Handle, GL_INFO_LOG_LENGTH, &length);
        std::string message(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
        glGetShaderInfoLog(m_Handle, static_cast<GLsizei>(message.size()), nullptr, message.data());
        throw std::runtime_error("OpenGL shader compilation failed: " + message);
      } catch (...) {
        glDeleteShader(m_Handle);
        throw;
      }
    }
  }
  ~Shader() { glDeleteShader(m_Handle); }
  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;
  GLuint Handle() const noexcept { return m_Handle; }

private:
  GLuint m_Handle;
};

} // namespace

OpenGLProgram::OpenGLProgram(const char* vertexSource, const char* fragmentSource)
{
  Shader vertex(GL_VERTEX_SHADER, vertexSource);
  Shader fragment(GL_FRAGMENT_SHADER, fragmentSource);
  m_Handle = glCreateProgram();
  if (m_Handle == 0) {
    throw std::runtime_error("Failed to create an OpenGL program.");
  }
  try {
    glAttachShader(m_Handle, vertex.Handle());
    glAttachShader(m_Handle, fragment.Handle());
    glLinkProgram(m_Handle);
    GLint linked = GL_FALSE;
    glGetProgramiv(m_Handle, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
      GLint length = 0;
      glGetProgramiv(m_Handle, GL_INFO_LOG_LENGTH, &length);
      std::string message(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
      glGetProgramInfoLog(m_Handle, static_cast<GLsizei>(message.size()), nullptr, message.data());
      throw std::runtime_error("OpenGL program linking failed: " + message);
    }
  } catch (...) {
    glDeleteProgram(m_Handle);
    throw;
  }
}

OpenGLProgram::~OpenGLProgram()
{
  glDeleteProgram(m_Handle);
}

GLint OpenGLProgram::Uniform(const char* name) const
{
  const auto location = glGetUniformLocation(m_Handle, name);
  if (location < 0) {
    throw std::runtime_error(std::string("OpenGL program is missing uniform: ") + name);
  }
  return location;
}

} // namespace Jade::Detail
