#pragma once
#include "../../../Platform/GLFW/GlfwRuntime.hpp"
#include <glad/gl.h>
#include <string>

struct GLFWwindow;
namespace Jade::Detail {
// Compute commands are a separate device capability, independent of draw calls.
class OpenGLComputeDevice final {
public:
  explicit OpenGLComputeDevice(bool shareWithCurrent = false);
  ~OpenGLComputeDevice();
  OpenGLComputeDevice(const OpenGLComputeDevice &) = delete;
  OpenGLComputeDevice &operator=(const OpenGLComputeDevice &) = delete;
  void current() const;
  void dispatch(GLuint x, GLuint y) const;
  void bindImage(GLuint texture, GLuint unit = 0, GLenum access = GL_WRITE_ONLY) const;
  void readbackBarrier() const;
  const std::string &name() const noexcept { return m_Name; }
  GLFWwindow *window() const noexcept { return m_Window; }
private:
  GlfwRuntime m_Runtime;
  GLFWwindow *m_Window = nullptr;
  using Dispatch = void (GLAD_API_PTR *)(GLuint, GLuint, GLuint);
  using Barrier = void (GLAD_API_PTR *)(GLbitfield);
  using BindImage = void (GLAD_API_PTR *)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
  Dispatch m_Dispatch = nullptr;
  Barrier m_Barrier = nullptr;
  BindImage m_BindImage = nullptr;
  std::string m_Name;
};
// Bindings are scoped so the compute context never remains current on a worker.
class ComputeContextScope final {
public:
  explicit ComputeContextScope(const OpenGLComputeDevice &);
  ~ComputeContextScope();
private:
  GLFWwindow *m_Previous;
};
inline constexpr GLenum ComputeShader = 0x91B9;
inline constexpr GLenum ShaderStorageBuffer = 0x90D2;
} // namespace Jade::Detail
