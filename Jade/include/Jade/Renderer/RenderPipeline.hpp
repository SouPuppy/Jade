#pragma once
#include <Jade/Renderer/Common/SceneSnapshot.hpp>
#include <span>
#include <chrono>

namespace Jade::Renderer {
enum class RenderMode { Raster, Hybrid, RayTracing, None };
constexpr const char* pipelineName(RenderMode mode) {
  switch (mode) {
    case RenderMode::Raster: return "Raster";
    case RenderMode::Hybrid: return "Hybrid";
    case RenderMode::RayTracing: return "RayTracing";
    case RenderMode::None: return "None";
  }
  return "None";
}
struct RenderTarget { ImageSize size; };
struct PipelineSettings {
  bool normals = false;
  float exposureEV = 0;
  bool cpuRayShadows = false;
  float shadowScale = .5f;
};
struct DebugViewOptions {
  bool entityAxes = true, bvh = true, cameraFrustums = true, lights = true;
};
// Backend owns resources, uploads, draw commands and synchronization.
class GraphicsDevice {
public:
  virtual ~GraphicsDevice() = default;
  virtual void drawScene(const SceneSnapshot&, RenderTarget, PipelineSettings, bool rayShadows) = 0;
  virtual void prepareScene(const SceneSnapshot&, RenderTarget, bool rayShadows) = 0;
  virtual void drawShadows(const SceneSnapshot&, bool rayShadows) = 0;
  virtual void drawGeometry(const SceneSnapshot&, PipelineSettings) = 0;
  virtual void composite(PipelineSettings) = 0;
  virtual void drawLines(std::span<const Vec3>, const Matrix4&, Vec3 color, bool depth) = 0;
};
struct RenderContext {
  GraphicsDevice& device;
  const SceneSnapshot& scene;
  RenderTarget target;
  PipelineSettings settings;
  bool paused = false;
  std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
};
class RenderPass {
public:
  virtual ~RenderPass() = default;
  virtual void execute(RenderContext&) = 0;
};
class RenderPipeline {
public:
  virtual ~RenderPipeline() = default;
  virtual const char* name() const noexcept = 0;
  virtual void render(RenderContext&) = 0;
};
} // namespace Jade::Renderer
