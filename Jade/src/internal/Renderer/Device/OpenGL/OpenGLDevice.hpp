#pragma once
#include "OpenGLContext.hpp"
#include <Jade/Graphics/Mesh.hpp>
#include <Jade/Renderer/Common/FrameBackend.hpp>
#include <glad/gl.h>
#include <memory>
#include <thread>
#include <unordered_map>
namespace Jade {
class Window;
}
namespace Jade::Renderer {
struct RasterizationConfig;
}
namespace Jade::Detail {
class RasterPipeline;
class GpuMesh final {
public:
  explicit GpuMesh(std::shared_ptr<const Mesh> resource);
  ~GpuMesh();
  void draw() const;
  std::shared_ptr<const Mesh> resource;
  GLuint vao = 0, vertices = 0, indices = 0;
};
class OpenGLDevice final {
public:
  OpenGLDevice(Window &, bool vsync);
  ~OpenGLDevice();
  void current() const;
  void present() const;
  RasterPipeline &raster(std::uint64_t id, const Renderer::RasterizationConfig &);
  GpuMesh &mesh(std::shared_ptr<const Mesh> resource);

private:
  std::thread::id m_Owner = std::this_thread::get_id();
  OpenGLContext m_Context;
  std::unique_ptr<RasterPipeline> m_Raster;
  std::uint64_t m_RasterId = 0;
  std::unordered_map<const void *, std::unique_ptr<GpuMesh>> m_Meshes;
};
class OpenGLFrameTarget final : public Renderer::FrameTarget {
public:
  explicit OpenGLFrameTarget(OpenGLDevice &device) : device(device) {}
  ~OpenGLFrameTarget();
  Renderer::ImageSize size() const noexcept override { return dimensions; }
  void resize(Renderer::ImageSize);
  void clear(Renderer::LinearRGB) override;
  void upload(std::span<const Renderer::LinearRGB>) override;
  OpenGLDevice &device;
  Renderer::ImageSize dimensions;
  GLuint framebuffer = 0, color = 0, depth = 0;
};
void CheckFramebuffer();
void Texture2D(GLuint &, Renderer::ImageSize, GLint format, GLenum type = GL_FLOAT);
} // namespace Jade::Detail
