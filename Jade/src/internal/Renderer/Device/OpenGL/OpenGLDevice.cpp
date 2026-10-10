#include "OpenGLDevice.hpp"
#include "../../../Platform/Window.hpp"
#include "../../../Mesh/MeshAccess.hpp"
#include "../../../../Renderer/Rasterization/RasterPipeline.hpp"
#include <cstddef>
#include <limits>
#include <stdexcept>
namespace Jade::Detail {
void CheckFramebuffer()
{
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error("Incomplete OpenGL rendering framebuffer.");
}
void Texture2D(GLuint &texture, Renderer::ImageSize size, GLint format, GLenum type)
{
  GLint maximum;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
  if (size.width > maximum || size.height > maximum)
    throw std::invalid_argument("Render target exceeds GL_MAX_TEXTURE_SIZE.");
  if (!texture)
    glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, format, size.width, size.height, 0,
               format == GL_DEPTH_COMPONENT32F ? GL_DEPTH_COMPONENT : GL_RGBA, type, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
GpuMesh::GpuMesh(std::shared_ptr<const Mesh> data) : resource(std::move(data))
{
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vertices);
  glGenBuffers(1, &indices);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vertices);
  const auto &mesh = resource->data();
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
               mesh.vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
               mesh.indices.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(offsetof(Vertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(offsetof(Vertex, normal)));
}
GpuMesh::~GpuMesh()
{
  glDeleteBuffers(1, &indices);
  glDeleteBuffers(1, &vertices);
  glDeleteVertexArrays(1, &vao);
}
void GpuMesh::draw() const
{
  glBindVertexArray(vao);
  constexpr std::size_t batch = 3000000;
  const auto count = resource->data().indices.size();
  for (std::size_t offset = 0; offset < count; offset += batch) {
    GLint program;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    const auto base = glGetUniformLocation(static_cast<GLuint>(program), "u_PrimitiveBase");
    glUniform1i(base, static_cast<GLint>(offset / 3));
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(std::min(batch, count - offset)), GL_UNSIGNED_INT,
                   reinterpret_cast<void *>(offset * sizeof(std::uint32_t)));
  }
}
OpenGLDevice::OpenGLDevice(Window &window, bool vsync)
    : m_Context(static_cast<GLFWwindow *>(window.NativeHandle()), vsync)
{
}
OpenGLDevice::~OpenGLDevice()
{
  current();
  m_Raster.reset();
  m_Meshes.clear();
}
void OpenGLDevice::current() const
{
  if (std::this_thread::get_id() != m_Owner)
    throw std::logic_error("OpenGL device used from a foreign thread.");
  m_Context.MakeCurrent();
}
void OpenGLDevice::present() const
{
  current();
  m_Context.Present();
}
RasterPipeline &OpenGLDevice::raster(std::uint64_t id, const Renderer::RasterizationConfig &config)
{
  current();
  if (!m_Raster || m_RasterId != id) {
    auto next = std::make_unique<RasterPipeline>(*this, config);
    m_Raster = std::move(next);
    m_RasterId = id;
  }
  return *m_Raster;
}
GpuMesh &OpenGLDevice::mesh(std::shared_ptr<const Mesh> resource)
{
  current();
  const auto key = MeshAccess::identity(*resource);
  auto it = m_Meshes.find(key);
  if (it != m_Meshes.end())
    return *it->second;
  auto mesh = std::make_unique<GpuMesh>(std::move(resource));
  auto &result = *mesh;
  m_Meshes.emplace(key, std::move(mesh));
  return result;
}
OpenGLFrameTarget::~OpenGLFrameTarget()
{
  device.current();
  glDeleteTextures(1, &color);
  glDeleteTextures(1, &depth);
  glDeleteFramebuffers(1, &framebuffer);
}
void OpenGLFrameTarget::resize(Renderer::ImageSize size)
{
  device.current();
  (void)size.pixels();
  if (dimensions == size)
    return;
  Texture2D(color, size, GL_RGBA32F);
  Texture2D(depth, size, GL_DEPTH_COMPONENT32F);
  if (!framebuffer)
    glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
  CheckFramebuffer();
  dimensions = size;
}
void OpenGLFrameTarget::clear(Renderer::LinearRGB rgb)
{
  device.current();
  if (!rgb.finite() || std::min({rgb.r, rgb.g, rgb.b}) < 0)
    throw std::invalid_argument("Invalid target radiance.");
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glViewport(0, 0, dimensions.width, dimensions.height);
  glDepthMask(GL_TRUE);
  glClearColor(rgb.r, rgb.g, rgb.b, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void OpenGLFrameTarget::upload(std::span<const Renderer::LinearRGB> pixels)
{
  if (pixels.size() != dimensions.pixels())
    throw std::invalid_argument("HDR upload dimensions differ from target.");
  for (auto rgb : pixels)
    if (!rgb.finite() || std::min({rgb.r, rgb.g, rgb.b}) < 0)
      throw std::invalid_argument("Invalid target radiance.");
  std::vector<Renderer::LinearRGB> flipped(pixels.size());
  for (int y = 0; y < dimensions.height; ++y)
    std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(y) * dimensions.width, dimensions.width,
                flipped.begin() + static_cast<std::ptrdiff_t>(dimensions.height - 1 - y) * dimensions.width);
  device.current();
  glBindTexture(GL_TEXTURE_2D, color);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, dimensions.width, dimensions.height, GL_RGB, GL_FLOAT,
                  flipped.data());
}
} // namespace Jade::Detail
