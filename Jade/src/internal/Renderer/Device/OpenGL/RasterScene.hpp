#pragma once
#include <Jade/Renderer/RenderPipeline.hpp>
#include <memory>
#include <Jade/Core/JobSystem.hpp>
namespace Jade::Detail {
// All GL resources and commands for the realtime passes belong to the backend.
class RasterScene final {
public:
  explicit RasterScene(JobSystem* jobs = nullptr);
  ~RasterScene();
  void draw(const Renderer::SceneSnapshot&, Renderer::RenderTarget, Renderer::PipelineSettings, bool rayShadows);
  void prepare(const Renderer::SceneSnapshot&, Renderer::RenderTarget, bool rayShadows);
  void shadows(const Renderer::SceneSnapshot&, bool rayShadows);
  void geometry(const Renderer::SceneSnapshot&, Renderer::PipelineSettings);
  void composite(Renderer::PipelineSettings);
private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
}
