#include "OpenGLDisplay.hpp"
#include <JadeShaders.hpp>

#include <array>
#include <limits>
#include <stdexcept>

namespace Jade::Detail {

OpenGLDisplay::OpenGLDisplay(OpenGLDevice &device) : m_Device(device)
{
  device.current();
  m_ImageProgram = std::make_unique<OpenGLProgram>(Shaders::display_vert, Shaders::image_frag);
  m_HdrProgram = std::make_unique<OpenGLProgram>(Shaders::display_vert, Shaders::hdr_frag);
  glGenVertexArrays(1, &m_Vao);
  glGenTextures(1, &m_Texture);
  glBindTexture(GL_TEXTURE_2D, m_Texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
OpenGLDisplay::~OpenGLDisplay()
{
  m_Device.current();
  m_ImageProgram.reset();
  m_HdrProgram.reset();
  glDeleteTextures(1, &m_Texture);
  glDeleteVertexArrays(1, &m_Vao);
}
void OpenGLDisplay::begin(Renderer::ImageSize size)
{
  m_Device.current();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, size.width, size.height);
  glDisable(GL_FRAMEBUFFER_SRGB);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDepthMask(GL_TRUE);
  glClearDepth(1);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void OpenGLDisplay::image(const Renderer::Image &image)
{
  if (image.pixels.size() != image.size.pixels() * 4)
    throw std::invalid_argument("Malformed display image.");
  glDisable(GL_DEPTH_TEST);
  glUseProgram(m_ImageProgram->Handle());
  glBindVertexArray(m_Vao);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_Texture);
  glUniform1i(m_ImageProgram->Uniform("u_Image"), 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  if (m_TextureSize != image.size) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.size.width, image.size.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image.pixels.data());
    m_TextureSize = image.size;
  } else
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.size.width, image.size.height, GL_RGBA, GL_UNSIGNED_BYTE,
                    image.pixels.data());
  glDrawArrays(GL_TRIANGLES, 0, 3);
}
void OpenGLDisplay::hdr(GLuint color, float exposure)
{
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glUseProgram(m_HdrProgram->Handle());
  glBindVertexArray(m_Vao);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, color);
  glUniform1i(m_HdrProgram->Uniform("u_Image"), 0);
  glUniform1f(m_HdrProgram->Uniform("u_Exposure"), exposure);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}
void OpenGLDisplay::present()
{
  glBindVertexArray(0);
  glUseProgram(0);
  m_Device.present();
}
} // namespace Jade::Detail
