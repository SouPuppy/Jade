#pragma once
#include "ProgressiveSession.hpp"
#include "../Device/OpenGL/OpenGLDevice.hpp"
#include "../Presentation/OpenGLDisplay.hpp"
#include "../Debug/DebugDraw.hpp"
#include <Jade/Renderer/RayTracing/RayTracing.hpp>
#include <Jade/Renderer/System.hpp>
#include "../Pipeline/RealtimePipeline.hpp"
#include "../Pipeline/RayTracingPipeline.hpp"

namespace Jade::Detail {
class RenderHost final {
public:
  RenderHost(Window &, JobSystem&, std::string title, Renderer::Config);
  ~RenderHost();
  Renderer::RayTracing &rayTracing() noexcept { return m_RayTracing; }
  Renderer::RayTracingSettings rayTracingSettings() const { return m_Settings; }
  void setRayTracingSettings(const Renderer::RayTracingSettings&);
  void setRayTracingEnabled(bool enabled) noexcept;
  void start(bool render);
  void stop() noexcept;
  void invalidate() noexcept;
  void rethrowError();
  Renderer::RenderProgress progress() const;
  std::shared_ptr<GpuFrame> gpuFrame() const { return m_Session.gpuFrame(); }
  void draw(const Renderer::SceneSnapshot *, Renderer::ImageSize, Renderer::RenderMode mode, bool wireframe, bool axes,
            const CollisionDebugLines* colliders = nullptr, bool paused = true,
            std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(),
            const Renderer::CameraFrame* observer=nullptr, const std::vector<Renderer::PathSegment>* paths=nullptr);

private:
  Window &m_Window;
  std::string m_Title;
  OpenGLDevice m_Device;
  OpenGLDisplay m_Display;
  DebugDraw m_Debug;
  Renderer::DebugViewOptions m_DebugOptions;
  RealtimePipeline m_Raster{false}, m_Hybrid{true};
  Renderer::RayTracingSettings m_Settings;
  Renderer::RayTracing m_RayTracing;
  std::unique_ptr<Renderer::ComputeRayTracer> m_Compute;
  ProgressiveSession m_Session;
  RayTracingPipeline m_RayPipeline{m_Session,m_Display,m_Settings};
  NonePipeline m_None;
  bool m_Render = true;
  bool m_Active = false;
  Renderer::ImageSize m_FrameSize;
};
} // namespace Jade::Detail
