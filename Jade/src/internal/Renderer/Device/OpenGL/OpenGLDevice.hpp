#pragma once
#include "OpenGLContext.hpp"
#include <glad/gl.h>
#include <thread>
#include <Jade/Renderer/RenderPipeline.hpp>
#include <memory>
namespace Jade {
class Window;
class JobSystem;
}
namespace Jade::Detail {
class RasterScene;
class OpenGLProgram;
class OpenGLDevice final : public Renderer::GraphicsDevice {
public:
  OpenGLDevice(Window &, bool vsync, JobSystem* jobs = nullptr);
  ~OpenGLDevice();
  void drawScene(const Renderer::SceneSnapshot&, Renderer::RenderTarget, Renderer::PipelineSettings, bool) override;
  void prepareScene(const Renderer::SceneSnapshot&, Renderer::RenderTarget, bool) override;
  void drawShadows(const Renderer::SceneSnapshot&, bool) override;
  void drawGeometry(const Renderer::SceneSnapshot&, Renderer::PipelineSettings) override;
  void composite(Renderer::PipelineSettings) override;
  void drawLines(std::span<const Vec3>, const Matrix4&, Vec3, bool) override;
  void current() const;
  void present() const;

private:
  std::thread::id m_Owner = std::this_thread::get_id();
  OpenGLContext m_Context;
  JobSystem* m_Jobs=nullptr;
  std::unique_ptr<RasterScene> m_Raster;
  std::unique_ptr<OpenGLProgram> m_Lines;
  GLuint m_LineVao=0, m_LineBuffer=0;
};
} // namespace Jade::Detail
