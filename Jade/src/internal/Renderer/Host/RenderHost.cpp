#include "RenderHost.hpp"
#include "../../Platform/Window.hpp"
#include "../../../Renderer/RayTracing/Settings.hpp"

namespace Jade::Detail {
namespace {
Renderer::RayTracingSettings cpuSettings(Renderer::RayTracingSettings settings) {
  settings.device = Renderer::RayDevice::CPU;
  return settings;
}
}
RenderHost::RenderHost(Window &window, JobSystem& jobs, std::string title, Renderer::Config config)
    : m_Window(window), m_Title(std::move(title)), m_Device(window, config.vsync), m_Display(m_Device),
      m_Debug(m_Device), m_Settings(config.rayTracing), m_RayTracing(jobs, cpuSettings(config.rayTracing)), m_Session(&jobs)
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
void RenderHost::draw(const Renderer::SceneSnapshot *snapshot, Renderer::ImageSize size, bool render, bool wireframe,
                      bool axes, const CollisionDebugLines* colliders, bool paused,
                      std::chrono::steady_clock::time_point deadline)
{
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
  const auto name = render ? std::string(m_Settings.device == Renderer::RayDevice::GPU ? m_Compute->name() : "RayTracing") +
    (wireframe ? " + Wireframe" : "") : (wireframe ? "Wireframe" : "Disabled");
  m_Window.SetTitle(m_Title + " | " + name);
  m_Display.begin(size);
  if (snapshot && snapshot->camera) {
    if (render) {
      m_Session.submit(*snapshot, size, paused, deadline);
      if (!paused) {
        m_Session.waitForFrame();
        m_Session.rethrowError();
      }
      if (auto frame = m_Session.gpuFrame())
        m_Display.image(frame, m_Settings.exposureEV, m_Settings.output == Renderer::RayOutput::Normal ?
          Renderer::ImageEncoding::LinearData : Renderer::ImageEncoding::Radiance);
      else if (auto image = m_Session.image())
        m_Display.image(*image);
    }
    if (wireframe)
      m_Debug.wireframe(*snapshot, size);
    if (axes)
      m_Debug.axes(*snapshot->camera, size);
    if (colliders)
      m_Debug.colliders(*colliders, *snapshot->camera, size);
  } else
    invalidate();
  m_Display.present();
}
} // namespace Jade::Detail
