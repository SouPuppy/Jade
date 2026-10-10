#include "Application.hpp"

#include "../internal/Core/FrameClock.hpp"
#include "../internal/Core/FrameDiagnostics.hpp"
#include "../internal/Math/Validation.hpp"
#include "../internal/Renderer/RenderSystem.hpp"

#include <Jade/Log.hpp>

#include <cmath>
#include <exception>
#include <optional>
#include <stdexcept>
#include <variant>

namespace Jade {
namespace {

const Log::Logger applicationLog{"Application"};
const Log::Logger frameLog{"Frame"};
const Log::Logger performanceLog{"Performance"};

} // namespace

Application::Application(const ApplicationConfig& config)
{
  if (config.frameLimits == 0) {
    throw std::invalid_argument("Frame limits must be positive.");
  }
  Detail::Validate(config.clearColor);
  m_Impl = std::make_unique<Impl>(config);
  applicationLog << Log::info("Ready: window={}x{} target_fps={} vsync={}",
    config.window.width, config.window.height, config.frameLimits, config.vsync ? "on" : "off");
}

Application::~Application() = default;

World& Application::world() noexcept { return m_Impl->world; }
Assets& Application::assets() noexcept { return m_Impl->assets; }
Entity Application::camera() const noexcept { return m_Impl->activeCamera; }

void Application::setCamera(Entity camera)
{
  if (!m_Impl->world.owns(camera) || !camera.has<Transform>() || !camera.has<Camera>()) {
    throw std::invalid_argument("Active camera requires Transform and Camera in this World.");
  }
  m_Impl->activeCamera = camera;
}

void Application::setWireframe(bool enabled) noexcept
{
  m_Impl->renderer.setWireframe(enabled);
}

void Application::setAxisGizmo(bool enabled) noexcept
{
  m_Impl->axisGizmo = enabled;
}

bool Application::axisGizmoEnabled() const noexcept
{
  return m_Impl->axisGizmo;
}

EventConnection Application::Connect(KeyAction action, KeyChord chord,
  std::function<EventFlow(const KeyEvent&)> callback)
{
  return EventConnection(m_Impl->events.Connect(action, chord, std::move(callback)));
}

EventConnection Application::Connect(std::function<EventFlow(const FrameUpdate&)> callback)
{
  return EventConnection(m_Impl->events.Connect(std::move(callback)));
}

EventConnection Application::Connect(MouseAction action, MouseButton button,
  std::function<EventFlow(const MouseEvent&)> callback)
{
  return EventConnection(m_Impl->events.Connect(action, button, std::move(callback)));
}

EventConnection Application::Connect(std::function<EventFlow(const MouseScrolled&)> callback)
{
  return EventConnection(m_Impl->events.Connect(std::move(callback)));
}

EventConnection Application::Connect(std::function<EventFlow(const MouseMoved&)> callback)
{
  return EventConnection(m_Impl->events.Connect(std::move(callback)));
}

void Application::stop() noexcept
{
  m_Impl->stopRequested = true;
}

void Application::Run(const RunOptions& options)
{
  if (m_Impl->running) {
    throw std::logic_error("Application::Run cannot be called from an event callback.");
  }
  m_Impl->running = true;
  m_Impl->stopRequested = false;
  using Clock = Detail::FrameClock::Clock;
  const auto start = Clock::now();
  std::uint64_t frames = 0;
  struct RunGuard {
    bool& running;
    const std::uint64_t& frames;
    Clock::time_point start;
    std::string_view reason = "window_closed";
    int exceptions = std::uncaught_exceptions();
    ~RunGuard()
    {
      running = false;
      const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
      if (std::uncaught_exceptions() > exceptions) {
        applicationLog << Log::error("Run aborted by exception: frames={} elapsed={:.2f}s", frames, elapsed);
      } else {
        applicationLog << Log::info("Run stopped: frames={} elapsed={:.2f}s reason={}", frames, elapsed, reason);
      }
    }
  } guard{m_Impl->running, frames, start};

  applicationLog << Log::info("Run started");
  Detail::FrameClock frameClock(m_Impl->frameLimits, start);
  Detail::FrameDiagnostics diagnostics(m_Impl->frameLimits);
  std::optional<Clock::time_point> previousFrame;

  while (!m_Impl->window.ShouldClose() && !m_Impl->stopRequested) {
    m_Impl->window.PollEvents();
    for (const auto& transition : m_Impl->window.Input().TakeEvents()) {
      if (const auto* key = std::get_if<Detail::KeyTransition>(&transition)) {
        const auto flow = m_Impl->events.Dispatch(key->action, key->event);
        if (key->action == KeyAction::Pressed && key->event.key == Key::Escape
            && flow == EventFlow::Continue && !m_Impl->stopRequested) {
          m_Impl->window.RequestClose();
        }
        if (key->action == KeyAction::Pressed && key->event.key == Key::F3
            && key->event.modifiers == Modifiers::None
            && flow == EventFlow::Continue && !m_Impl->stopRequested) {
          setAxisGizmo(!axisGizmoEnabled());
          applicationLog << Log::info("Axis Gizmo: {}", axisGizmoEnabled() ? "on" : "off");
        }
      } else if (const auto* mouse = std::get_if<Detail::MouseTransition>(&transition)) {
        m_Impl->events.Dispatch(mouse->action, mouse->event);
      } else if (const auto* scroll = std::get_if<MouseScrolled>(&transition)) {
        m_Impl->events.Dispatch(*scroll);
      } else {
        m_Impl->events.Dispatch(std::get<MouseMoved>(transition));
      }
      if (m_Impl->window.ShouldClose() || m_Impl->stopRequested) {
        break;
      }
    }
    if (m_Impl->window.ShouldClose() || m_Impl->stopRequested) {
      break;
    }

    const auto size = m_Impl->window.GetFramebufferSize();
    if (size.width <= 0 || size.height <= 0) {
      m_Impl->window.WaitEvents(0.05);
      frameClock.Resume(Clock::now());
      previousFrame.reset();
      diagnostics.Resume();
      continue;
    }

    const auto now = Clock::now();
    if (now < frameClock.NextDeadline()) {
      // Events wake this wait early; check the absolute deadline again before drawing.
      m_Impl->window.WaitEvents(
        std::chrono::duration<double>(frameClock.NextDeadline() - now).count());
      continue;
    }

    const float dt = previousFrame
      ? std::chrono::duration<float>(now - *previousFrame).count() : 0.0f;
    previousFrame = now;
    const auto modifiers = m_Impl->window.Input().CurrentModifiers();
    for (std::size_t index = 1; index < Detail::InputState::keyCount; ++index) {
      const auto key = static_cast<Key>(index);
      if (m_Impl->window.Input().IsDown(key)) {
        m_Impl->events.Dispatch(KeyAction::Held, {key, modifiers, dt});
      }
      if (m_Impl->stopRequested || m_Impl->window.ShouldClose()) {
        break;
      }
    }
    if (m_Impl->stopRequested || m_Impl->window.ShouldClose()) {
      break;
    }
    for (std::size_t index = 1; index < Detail::InputState::buttonCount; ++index) {
      const auto button = static_cast<MouseButton>(index);
      if (m_Impl->window.Input().IsDown(button)) {
        m_Impl->events.Dispatch(MouseAction::Held, {button, modifiers, dt});
      }
      if (m_Impl->stopRequested || m_Impl->window.ShouldClose()) {
        break;
      }
    }
    if (m_Impl->stopRequested || m_Impl->window.ShouldClose()) {
      break;
    }
    m_Impl->events.Dispatch(FrameUpdate{dt,
      std::chrono::duration<double>(now - start).count(), frames + 1});
    if (m_Impl->stopRequested || m_Impl->window.ShouldClose()) {
      break;
    }

    m_Impl->renderer.BeginFrame(size, m_Impl->clearColor);
    if (options.validateRendering &&
      !m_Impl->renderer.ValidateClear(m_Impl->clearColor)) {
      throw std::runtime_error("Framebuffer clear validation failed.");
    }

    Detail::RenderSystem::Render(m_Impl->world, m_Impl->assets, m_Impl->activeCamera,
      m_Impl->renderer, size, m_Impl->axisGizmo);
    m_Impl->renderer.Present();
    const auto completed = Clock::now();
    const auto stats = frameClock.FinishFrame(completed);
    frames = stats.frame;
    if (m_Impl->logPerformance) {
      const auto sample = diagnostics.FinishFrame(now, completed);
      frameLog << Log::trace("frame={} interval={:.2f}ms work={:.2f}ms",
        frames, sample.intervalMs, sample.workMs);
      if (sample.hitch) {
        performanceLog << Log::warn("Frame hitch: frame={} interval={:.2f}ms budget={:.2f}ms",
          frames, sample.intervalMs, 1000.0 / m_Impl->frameLimits);
      }
      if (sample.summary) {
        const auto& summary = *sample.summary;
        performanceLog << Log::debug(
          "fps={:.1f} interval_avg={:.2f}ms interval_max={:.2f}ms work_avg={:.2f}ms work_max={:.2f}ms",
          summary.fps, summary.intervalAvgMs, summary.intervalMaxMs,
          summary.workAvgMs, summary.workMaxMs);
      }
    }
    if (options.frameLimit != 0 && frames >= options.frameLimit) {
      break;
    }
  }

  if (options.frameLimit != 0 && frames != options.frameLimit && !m_Impl->stopRequested) {
    throw std::runtime_error("Application closed before the frame limit.");
  }
  if (m_Impl->stopRequested) {
    guard.reason = "requested";
  } else if (options.frameLimit != 0 && frames >= options.frameLimit) {
    guard.reason = "frame_limit";
  }
}

} // namespace Jade
