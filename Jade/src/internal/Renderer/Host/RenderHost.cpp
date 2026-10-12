#include "RenderHost.hpp"
#include "../../Platform/Window.hpp"
#include "../../../Renderer/RayTracing/Settings.hpp"
#include <array>

namespace Jade::Detail {
namespace {
Renderer::RayTracingSettings cpuSettings(Renderer::RayTracingSettings settings) {
  settings.device = Renderer::RayDevice::CPU;
  return settings;
}
}
RenderHost::RenderHost(Window &window, JobSystem& jobs, std::string title, Renderer::Config config)
    : m_Window(window), m_Title(std::move(title)), m_Device(window, config.vsync, &jobs), m_Display(m_Device),
      m_Debug(m_Device), m_DebugOptions(config.debugView), m_Settings(config.rayTracing), m_RayTracing(jobs, cpuSettings(config.rayTracing)), m_Session(&jobs)
{
  validateRayTracingSettings(m_Settings);
  if (m_Settings.device == Renderer::RayDevice::GPU) {
    m_Compute.reset(new Renderer::ComputeRayTracer(m_Settings, true));
    m_Session.bind(*m_Compute);
  } else m_Session.bind(m_RayTracing);
}
void RenderHost::setRayTracingSettings(const Renderer::RayTracingSettings& settings)
{
  validateRayTracingSettings(settings);
  if (settings == m_Settings) return;
  rethrowError();
  const bool reset = settings.device != m_Settings.device || settings.mode != m_Settings.mode || settings.output != m_Settings.output ||
    settings.jitter != m_Settings.jitter || settings.resolutionScale != m_Settings.resolutionScale ||
    settings.seed != m_Settings.seed ||
    (settings.mode == Renderer::RayMode::PathTrace &&
      (settings.maxBounces != m_Settings.maxBounces || settings.rrStartBounce != m_Settings.rrStartBounce));
  if (!reset) {
    m_Session.updateSettings(settings);
    if (settings.enabled != m_Settings.enabled) invalidate();
    m_Settings = settings;
    return;
  }
  // Create the GPU context on the window thread before stopping a working backend.
  std::unique_ptr<Renderer::ComputeRayTracer> candidate;
  if (settings.device == Renderer::RayDevice::GPU && !m_Compute) {
    m_Device.current();
    candidate.reset(new Renderer::ComputeRayTracer(settings, true));
  }
  m_Session.stop();
  if (settings.device == Renderer::RayDevice::GPU) {
    if (candidate) m_Compute = std::move(candidate);
    else m_Compute->configure(settings);
    m_Session.bind(*m_Compute);
  } else {
    m_RayTracing.configure(settings);
    m_Session.bind(m_RayTracing);
  }
  m_Settings = settings;
  if (m_Active && m_Render && m_Settings.enabled) m_Session.start();
}
void RenderHost::setRayTracingEnabled(bool enabled) noexcept
{
  if (enabled != m_Settings.enabled) {
    m_Settings.enabled = enabled;
    invalidate();
  }
}
RenderHost::~RenderHost() { stop(); }
void RenderHost::start(bool render)
{
  stop();
  invalidate();
  m_Render = render;
  m_Active = true;
  m_RayTracing.m_ApplicationActive = true;
  try {
    if (render)
      m_Session.start();
  } catch (...) {
    stop();
    throw;
  }
}
void RenderHost::stop() noexcept
{
  m_Session.stop();
  m_RayTracing.m_ApplicationActive = false;
  m_Active = false;
}
void RenderHost::invalidate() noexcept { m_Session.invalidate(); }
void RenderHost::rethrowError() { m_Session.rethrowError(); }
Renderer::RenderProgress RenderHost::progress() const
{
  if (!m_Render) {
    Renderer::RenderProgress result;
    result.rendererName = "Disabled";
    return result;
  }
  auto result = m_Session.progress();
  result.rendererName = m_Settings.device == Renderer::RayDevice::GPU ? m_Compute->name() : "RayTracing";
  return result;
}
void RenderHost::draw(const Renderer::SceneSnapshot *snapshot, Renderer::ImageSize size, Renderer::RenderMode mode, bool wireframe,
                      bool axes, const CollisionDebugLines* colliders, bool paused,
                      std::chrono::steady_clock::time_point deadline, const Renderer::CameraFrame* observer,
                      const std::vector<Renderer::PathSegment>* paths, bool normalDebug)
{
  std::optional<Renderer::SceneSnapshot> view;
  const auto sourceCamera = snapshot ? snapshot->camera : std::optional<Renderer::CameraFrame>{};
  if(observer && snapshot) {
    view=*snapshot; view->camera=*observer; snapshot=&*view;
    mode=Renderer::RenderMode::Raster;
  }
  const bool render = !normalDebug && mode == Renderer::RenderMode::RayTracing;
  if (m_Render != render) {
    m_Render = render;
    m_Session.stop();
    invalidate();
    rethrowError();
    if (render)
      m_Session.start();
  }
  if (m_FrameSize != size) {
    invalidate();
    m_FrameSize = size;
  }
  const auto name = normalDebug ? std::string("Normals") + (wireframe ? " + Wireframe" : "") :
    render ? std::string(m_Settings.device == Renderer::RayDevice::GPU ? m_Compute->name() : "RayTracing") +
    (wireframe ? " + Wireframe" : "") : std::string(Renderer::pipelineName(mode)) + (wireframe ? " + Wireframe" : "");
  m_Window.SetTitle(m_Title + " | " + name + (observer ? " | Soul" : ""));
  m_Display.begin(size);
  if (snapshot && snapshot->camera) {
    Renderer::RenderContext context{m_Device,*snapshot,{size},{m_Settings.output==Renderer::RayOutput::Normal,m_Settings.exposureEV,m_Settings.device==Renderer::RayDevice::CPU,m_Settings.resolutionScale},paused,deadline};
    // Sampling is only part of a frame: reserve time for importing/presenting
    // the shared image and returning to the window deadline. The minimum full
    // sample floor remains enforced by ProgressiveSession; paused accumulation
    // ignores this deadline and can continue refining the image.
    constexpr auto presentationReserve=std::chrono::milliseconds(6);
    if(deadline!=std::chrono::steady_clock::time_point::max())
      context.deadline=deadline>=std::chrono::steady_clock::time_point::min()+presentationReserve?
        deadline-presentationReserve:std::chrono::steady_clock::time_point::min();
    Renderer::RenderPipeline* pipeline=&m_None;
    switch(mode) {
      case Renderer::RenderMode::Raster:pipeline=&m_Raster;break;
      case Renderer::RenderMode::Hybrid:pipeline=&m_Hybrid;break;
      case Renderer::RenderMode::RayTracing:pipeline=&m_RayPipeline;break;
      case Renderer::RenderMode::None:break;
    }
    if (normalDebug) pipeline = &m_NormalDebug;
    pipeline->render(context);
    if (wireframe)
      m_Debug.wireframe(*snapshot, size);
    if (axes)
      m_Debug.axes(*snapshot->camera, size);
    if (axes) m_Debug.scene(*snapshot,size,m_DebugOptions);
    if (colliders)
      m_Debug.colliders(*colliders, *snapshot->camera, size);
    if(observer && sourceCamera) {
      m_Debug.frustum(*sourceCamera,*observer,size);
      if(paths) {
        const std::array<Vec3,5> colors{{{0,.8f,1},{1,.5f,0},{.2f,1,.2f},{1,.15f,.15f},{.6f,.6f,.6f}}};
        for(std::size_t i=0;i<colors.size();++i) {
          std::vector<Vec3> points;
          for(const auto& segment:*paths) if(static_cast<std::size_t>(segment.kind)==i) points.insert(points.end(),{segment.start,segment.end});
          m_Debug.paths(points,*observer,size,colors[i]);
        }
      }
    }
  } else
    invalidate();
  if (!m_OverlayText.empty()) m_Debug.text(m_OverlayText,size);
  m_Display.present();
}
} // namespace Jade::Detail
