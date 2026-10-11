#pragma once
#include "../Device/OpenGL/OpenGLDevice.hpp"
#include "../Device/OpenGL/OpenGLProgram.hpp"
#include <Jade/Renderer/Common/Image.hpp>
#include "../Device/OpenGL/GpuFrame.hpp"
#include <memory>
namespace Jade::Detail {
class OpenGLDisplay final {
public:
  explicit OpenGLDisplay(OpenGLDevice &);
  ~OpenGLDisplay();
  void begin(Renderer::ImageSize);
  void image(const Renderer::Image &);
  void image(const std::shared_ptr<GpuFrame>&, float exposureEV, Renderer::ImageEncoding);

  void present();

private:
  OpenGLDevice &m_Device;
  std::unique_ptr<OpenGLProgram> m_ImageProgram;
  GLuint m_Vao = 0, m_Texture = 0;
  Renderer::ImageSize m_TextureSize;
};
} // namespace Jade::Detail
