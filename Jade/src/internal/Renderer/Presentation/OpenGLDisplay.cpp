#include "OpenGLDisplay.hpp"

#include <array>
#include <limits>
#include <stdexcept>

namespace Jade::Detail {
namespace {
constexpr const char *imageVertex = R"(#version 330 core
out vec2 uv;
void main() {
  vec2 p=vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  uv=vec2(p.x,1.0-p.y);
  gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
)";
constexpr const char *imageFragment = R"(#version 330 core
in vec2 uv;
uniform sampler2D u_Image;
out vec4 fragmentColor;
void main() { fragmentColor=texture(u_Image,uv); }
)";
constexpr const char *hdrFragment = R"(#version 330 core
in vec2 uv;
uniform sampler2D u_Image;
uniform float u_Exposure;
out vec4 fragmentColor;
vec3 encode(vec3 c) { return mix(12.92*c,1.055*pow(c,vec3(1.0/2.4))-0.055,step(vec3(0.0031308),c)); }
void main() {
  vec3 c=max(texture(u_Image,vec2(uv.x,1.0-uv.y)).rgb,vec3(0));
  vec3 mapped=c/(c+vec3(exp2(-u_Exposure)));
  fragmentColor=vec4(encode(mapped),1);
}
)";
} // namespace

OpenGLDisplay::OpenGLDisplay(OpenGLDevice& device) : m_Device(device) {
  device.current();
  m_ImageProgram=std::make_unique<OpenGLProgram>(imageVertex,imageFragment);
  m_HdrProgram=std::make_unique<OpenGLProgram>(imageVertex,hdrFragment);
  glGenVertexArrays(1,&m_Vao);glGenTextures(1,&m_Texture);
  glBindTexture(GL_TEXTURE_2D,m_Texture);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}
OpenGLDisplay::~OpenGLDisplay() {
  m_Device.current();m_ImageProgram.reset();m_HdrProgram.reset();glDeleteTextures(1,&m_Texture);glDeleteVertexArrays(1,&m_Vao);
}
void OpenGLDisplay::begin(Renderer::ImageSize size)
{
  m_Device.current();
  glBindFramebuffer(GL_FRAMEBUFFER,0);
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
void OpenGLDisplay::hdr(GLuint color, float exposure) {
  glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
  glUseProgram(m_HdrProgram->Handle());glBindVertexArray(m_Vao);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,color);
  glUniform1i(m_HdrProgram->Uniform("u_Image"),0);glUniform1f(m_HdrProgram->Uniform("u_Exposure"),exposure);
  glDrawArrays(GL_TRIANGLES,0,3);
}
void OpenGLDisplay::present()
{
  glBindVertexArray(0);
  glUseProgram(0);
  m_Device.present();
}
} // namespace Jade::Detail
