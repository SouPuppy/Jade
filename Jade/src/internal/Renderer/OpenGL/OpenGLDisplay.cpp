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
constexpr const char *lineVertex = R"(#version 330 core
layout(location=0) in vec3 a_Position;
uniform mat4 u_Transform;
void main() { gl_Position=u_Transform*vec4(a_Position,1.0); }
)";
constexpr const char *lineFragment = R"(#version 330 core
uniform vec3 u_Color;
out vec4 fragmentColor;
void main() { fragmentColor=vec4(u_Color,1.0); }
)";
} // namespace

OpenGLDisplay::OpenGLDisplay(Window &window, bool vsync)
    : m_Context(static_cast<GLFWwindow *>(window.NativeHandle()), vsync)
{
  try {
    m_ImageProgram = std::make_unique<OpenGLProgram>(imageVertex, imageFragment);
    m_LineProgram = std::make_unique<OpenGLProgram>(lineVertex, lineFragment);
    glGenVertexArrays(1, &m_Vao);
    glGenBuffers(1, &m_Buffer);
    glGenTextures(1, &m_Texture);
    if (!m_Vao || !m_Buffer || !m_Texture)
      throw std::runtime_error("Failed to allocate image/debug objects.");
    glBindVertexArray(m_Vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_Buffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), nullptr);
    glBindTexture(GL_TEXTURE_2D, m_Texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  } catch (...) {
    glDeleteTextures(1, &m_Texture);
    glDeleteBuffers(1, &m_Buffer);
    glDeleteVertexArrays(1, &m_Vao);
    throw;
  }
}
OpenGLDisplay::~OpenGLDisplay()
{
  m_Context.MakeCurrent();
  m_LineProgram.reset();
  m_ImageProgram.reset();
  glDeleteTextures(1, &m_Texture);
  glDeleteBuffers(1, &m_Buffer);
  glDeleteVertexArrays(1, &m_Vao);
}
void OpenGLDisplay::begin(Renderer::ImageSize size)
{
  m_Context.MakeCurrent();
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
void OpenGLDisplay::lines(std::span<const Vec3> points, const Matrix4 &transform, Vec3 color, bool depth)
{
  if (points.empty())
    return;
  glUseProgram(m_LineProgram->Handle());
  glBindVertexArray(m_Vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_Buffer);
  glUniformMatrix4fv(m_LineProgram->Uniform("u_Transform"), 1, GL_FALSE, transform.data());
  glUniform3f(m_LineProgram->Uniform("u_Color"), color.x, color.y, color.z);
  if (depth)
    glEnable(GL_DEPTH_TEST);
  else
    glDisable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(depth ? GL_TRUE : GL_FALSE);
  // Bound each upload/draw independently of the CPU mesh's total index count.
  constexpr std::size_t batch = 65536;
  for (std::size_t offset = 0; offset < points.size(); offset += batch) {
    const auto count = std::min(batch, points.size() - offset);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * sizeof(Vec3)), points.data() + offset,
                 GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(count));
  }
  glDepthMask(GL_TRUE);
  glDisable(GL_DEPTH_TEST);
}
void OpenGLDisplay::wireframe(const Renderer::SceneSnapshot &snapshot, Renderer::ImageSize size)
{
  if (!snapshot.camera)
    return;
  const auto projection = snapshot.camera->viewProjection(size);
  std::vector<Vec3> edges;
  for (const auto &instance : snapshot.instances) {
    const auto &mesh = instance.mesh->data();
    edges.clear();
    edges.reserve(mesh.indices.size() * 2);
    for (std::size_t index = 0; index < mesh.indices.size(); index += 3) {
      const Vec3 a = mesh.vertices[mesh.indices[index]].position,
                 b = mesh.vertices[mesh.indices[index + 1]].position,
                 c = mesh.vertices[mesh.indices[index + 2]].position;
      edges.insert(edges.end(), {a, b, b, c, c, a});
    }
    lines(edges, projection * instance.objectToWorld, {0.8f, 0.85f, 0.9f}, true);
  }
}
void OpenGLDisplay::axes(const Renderer::CameraFrame &camera, Renderer::ImageSize size)
{
  const auto projection = camera.viewProjection(size);
  const std::array<Vec3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  for (const auto axis : axes) {
    const std::array<Vec3, 2> points{{{}, axis}};
    lines(points, projection, axis, false);
  }
}
void OpenGLDisplay::present()
{
  glBindVertexArray(0);
  glUseProgram(0);
  m_Context.Present();
}
} // namespace Jade::Detail
