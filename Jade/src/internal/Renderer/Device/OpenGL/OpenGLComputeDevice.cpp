#include "OpenGLComputeDevice.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace Jade::Detail {
OpenGLComputeDevice::OpenGLComputeDevice(bool shareWithCurrent)
{
  auto *previous = glfwGetCurrentContext();
  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  if (shareWithCurrent && !previous) throw std::logic_error("A current display context is required for GPU presentation.");
  m_Window = glfwCreateWindow(1, 1, "Jade Compute", nullptr, shareWithCurrent ? previous : nullptr);
  glfwDefaultWindowHints();
  if (!m_Window) throw std::runtime_error("GPU ray casting requires OpenGL 4.3 compute support.");
  try {
    current();
    if (!glCreateShader && !gladLoadGL(glfwGetProcAddress))
      throw std::runtime_error("Failed to load OpenGL for compute rendering.");
    m_Dispatch = reinterpret_cast<Dispatch>(glfwGetProcAddress("glDispatchCompute"));
    m_Barrier = reinterpret_cast<Barrier>(glfwGetProcAddress("glMemoryBarrier"));
    m_BindImage = reinterpret_cast<BindImage>(glfwGetProcAddress("glBindImageTexture"));
    if (!m_Dispatch || !m_Barrier || !m_BindImage)
      throw std::runtime_error("Required OpenGL compute entry points are unavailable.");
    const auto *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    m_Name = renderer ? renderer : "Unknown";
    GLint blocks = 0;
    glGetIntegerv(0x90DB /* GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS */, &blocks);
    if (blocks < 8) throw std::runtime_error("GPU path tracing requires eight compute SSBO bindings.");
    glfwMakeContextCurrent(previous);
  } catch (...) {
    glfwMakeContextCurrent(previous);
    glfwDestroyWindow(m_Window);
    m_Window = nullptr;
    throw;
  }
}
OpenGLComputeDevice::~OpenGLComputeDevice() { glfwDestroyWindow(m_Window); }
void OpenGLComputeDevice::current() const { glfwMakeContextCurrent(m_Window); }
void OpenGLComputeDevice::dispatch(GLuint x, GLuint y) const { m_Dispatch(x, y, 1); }
void OpenGLComputeDevice::bindImage(GLuint texture, GLuint unit, GLenum access) const
{
  m_BindImage(unit, texture, 0, GL_FALSE, 0, access, GL_RGBA32F);
}
void OpenGLComputeDevice::readbackBarrier() const
{
  m_Barrier(0x00000100 /* TEXTURE_UPDATE */ | 0x00000008 /* TEXTURE_FETCH */ |
            0x00000020 /* SHADER_IMAGE_ACCESS */ | 0x00000200 /* BUFFER_UPDATE */);
}
ComputeContextScope::ComputeContextScope(const OpenGLComputeDevice &device)
  : m_Previous(glfwGetCurrentContext()) { device.current(); }
ComputeContextScope::~ComputeContextScope() { glfwMakeContextCurrent(m_Previous); }
} // namespace Jade::Detail
