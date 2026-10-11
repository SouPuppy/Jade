#pragma once
#include <Jade/Renderer/RenderPipeline.hpp>
#include <memory>
#include <vector>
namespace Jade::Detail {
class SceneUploadPass final : public Renderer::RenderPass {
public:
  explicit SceneUploadPass(bool rayShadows) : m_RayShadows(rayShadows) {}
  void execute(Renderer::RenderContext& c) override {
    c.device.prepareScene(c.scene,c.target,m_RayShadows);
  }
private:
  bool m_RayShadows;
};
class ShadowPass final : public Renderer::RenderPass {
public:
  explicit ShadowPass(bool rayShadows):m_RayShadows(rayShadows) {}
  void execute(Renderer::RenderContext& c) override {c.device.drawShadows(c.scene,m_RayShadows);}
private:
  bool m_RayShadows;
};
class GeometryLightingPass final : public Renderer::RenderPass {
public:
  void execute(Renderer::RenderContext& c) override {c.device.drawGeometry(c.scene,c.settings);}
};
class CompositePass final : public Renderer::RenderPass {
public:
  void execute(Renderer::RenderContext& c) override {c.device.composite(c.settings);}
};
class RealtimePipeline final : public Renderer::RenderPipeline {
public:
  explicit RealtimePipeline(bool hybrid) : m_Hybrid(hybrid) {
    m_Passes.push_back(std::make_unique<SceneUploadPass>(hybrid));
    m_Passes.push_back(std::make_unique<ShadowPass>(hybrid));
    m_Passes.push_back(std::make_unique<GeometryLightingPass>());
    m_Passes.push_back(std::make_unique<CompositePass>());
  }
  const char* name() const noexcept override { return m_Hybrid ? "Hybrid" : "Raster"; }
  void render(Renderer::RenderContext& c) override { for (auto& pass : m_Passes) pass->execute(c); }
private:
  bool m_Hybrid;
  std::vector<std::unique_ptr<Renderer::RenderPass>> m_Passes;
};
}
