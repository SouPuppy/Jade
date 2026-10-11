#include "Application.hpp"
#include "Scene.hpp"

#include "../internal/Core/FrameClock.hpp"
#include "../internal/Core/FrameDiagnostics.hpp"
#include "../internal/Math/Validation.hpp"

#include <Jade/Log.hpp>
#include <Jade/Core/Transform.hpp>
#include <Jade/Graphics/Camera.hpp>

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
const Log::Logger rayLog{"RayTracing"};

} // namespace

Application::Application(const EngineConfig &config)
{
  if (config.frameLimits == 0) {
    throw std::invalid_argument("Frame limits must be positive.");
  }
  m_Impl = std::make_unique<Impl>(config);
  applicationLog << Log::info("Ready: window={}x{} target_fps={} vsync={}", config.window.width,
                              config.window.height, config.frameLimits, config.renderer.vsync ? "on" : "off");
}

Application::~Application() = default;
Runtime& Application::runtime() noexcept { return m_Impl->runtime; }
EventConnection Application::Connect(KeyAction action, KeyChord chord,
                                     std::function<EventFlow(const KeyEvent &)> callback)
{
  return EventConnection(m_Impl->events.Connect(action, chord, std::move(callback)));
}

EventConnection Application::Connect(std::function<EventFlow(const FrameUpdate &)> callback)
{
  return EventConnection(m_Impl->events.Connect(std::move(callback)));
}

EventConnection Application::Connect(MouseAction action, MouseButton button,
                                     std::function<EventFlow(const MouseEvent &)> callback)
{
  return EventConnection(m_Impl->events.Connect(action, button, std::move(callback)));
}

EventConnection Application::Connect(std::function<EventFlow(const MouseScrolled &)> callback)
{
  return EventConnection(m_Impl->events.Connect(std::move(callback)));
}

EventConnection Application::Connect(std::function<EventFlow(const MouseMoved &)> callback)
{
  return EventConnection(m_Impl->events.Connect(std::move(callback)));
}

void Application::stop() noexcept { m_Impl->stopRequested = true; }

void Application::Run(const RunOptions &options)
{
  if (m_Impl->running) {
    throw std::logic_error("Application::Run cannot be called from an event callback.");
  }
  m_Impl->runtime.begin();
  m_Impl->running = true;
  m_Impl->stopRequested = false;
  using Clock = Detail::FrameClock::Clock;
  const auto start = Clock::now();
  std::uint64_t frames = 0;
  struct RunGuard {
    Runtime &runtime;
    bool &running;
    const std::uint64_t &frames;
    Clock::time_point start;

    std::string_view reason = "window_closed";
    int exceptions = std::uncaught_exceptions();
    ~RunGuard()
    {
      if (std::uncaught_exceptions() > exceptions) runtime.abort();
      else runtime.end();
      running = false;
      const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
      if (std::uncaught_exceptions() > exceptions) {

        applicationLog << Log::error("Run aborted by exception: frames={} elapsed={:.2f}s", frames, elapsed);
      } else {
        applicationLog << Log::info("Run stopped: frames={} elapsed={:.2f}s reason={}", frames, elapsed,
                                    reason);
      }
    }
  } guard{m_Impl->runtime, m_Impl->running, frames, start};

  applicationLog << Log::info("Run started");
  Detail::FrameClock frameClock(m_Impl->frameLimits, start);
  Detail::FrameDiagnostics diagnostics(m_Impl->frameLimits);
  std::optional<Clock::time_point> previousFrame;


  while (!m_Impl->window.ShouldClose() && !m_Impl->stopRequested) {
    if (m_Impl->runtime.commitScene()) {
      previousFrame.reset();
      diagnostics.Resume();
      frameClock.Resume(Clock::now());
    }
    if (m_Impl->stopRequested)
      break;
    // One processing pass retains the same scene through input, update and present.
    const auto active = m_Impl->runtime.scene();
    // Also covers a module installed after this Scene became active.
    m_Impl->window.SetMouseCaptured(active && active->CapturesMouse());
    const auto dispatch = [&](auto &&...arguments) {
      const auto flow = m_Impl->events.Dispatch(arguments...);
      if (flow == EventFlow::Stop || !active || m_Impl->stopRequested || m_Impl->window.ShouldClose())
        return flow;
      return active->m_Impl->events.Dispatch(arguments...);
    };
    m_Impl->window.PollEvents();
    for (const auto &transition : m_Impl->window.Input().TakeEvents()) {
      if (const auto *key = std::get_if<Detail::KeyTransition>(&transition)) {
        const auto flow = dispatch(key->action, key->event);
        if (key->action == KeyAction::Pressed && key->event.key == Key::Escape &&
            flow == EventFlow::Continue && !m_Impl->stopRequested) {
          m_Impl->window.RequestClose();
        }
        if (key->action == KeyAction::Pressed && (key->event.key == Key::I || key->event.key == Key::F) &&
            key->event.modifiers == Modifiers::None && flow == EventFlow::Continue &&
            !m_Impl->stopRequested && !m_Impl->window.ShouldClose()) {
          auto& renderer = runtime().renderer();
          if (key->event.key == Key::I) {
            renderer.setRender(!renderer.renderEnabled());
            applicationLog << Log::info("Render: {}", renderer.renderEnabled() ? "on" : "off");
          } else {
            renderer.setWireframe(!renderer.wireframeEnabled());
            applicationLog << Log::info("Wireframe: {}", renderer.wireframeEnabled() ? "on" : "off");
          }
        }
        if (m_Impl->debug && key->action == KeyAction::Pressed && key->event.modifiers == Modifiers::None &&
            flow == EventFlow::Continue && !m_Impl->stopRequested && !m_Impl->window.ShouldClose()) {
          if (key->event.key == Key::F3) {
            const bool enabled = !(runtime().axesEnabled() || runtime().aabbsEnabled());
            runtime().setAxes(enabled); runtime().setAabbs(enabled);
            applicationLog << Log::info("Axes/AABB: {}", enabled ? "on" : "off");
          } else if (key->event.key == Key::F4) {
            runtime().setPhysicsDebug(!runtime().physicsDebugEnabled());
          } else if (key->event.key == Key::O && active) {
            const auto camera = active->camera();
            if (camera.has<Transform>() && camera.has<Camera>()) {
              auto &lens = camera.get<Camera>();
              lens.projection = lens.projection == Projection::Perspective ? Projection::Orthographic
                                                                           : Projection::Perspective;
              applicationLog << Log::info("Projection: {}", lens.projection == Projection::Perspective
                                                                ? "Perspective"
                                                                : "Orthographic");
            }
          }
        }
      } else if (const auto *mouse = std::get_if<Detail::MouseTransition>(&transition)) {
        dispatch(mouse->action, mouse->event);
      } else if (const auto *scroll = std::get_if<MouseScrolled>(&transition)) {
        dispatch(*scroll);
      } else {
        dispatch(std::get<MouseMoved>(transition));
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
      m_Impl->runtime.suspend();
      previousFrame.reset();
      m_Impl->window.WaitEvents(0.05);
      frameClock.Resume(Clock::now());
      previousFrame.reset();
      diagnostics.Resume();
      continue;
    }

    const auto now = Clock::now();
    if (now < frameClock.NextDeadline()) {
      // Events wake this wait early; check the absolute deadline again before drawing.
      m_Impl->window.WaitEvents(std::chrono::duration<double>(frameClock.NextDeadline() - now).count());
      continue;
    }

    const float dt = previousFrame ? std::chrono::duration<float>(now - *previousFrame).count() : 0.0f;
    previousFrame = now;
    const auto modifiers = m_Impl->window.Input().CurrentModifiers();
    for (std::size_t index = 1; index < Detail::InputState::keyCount; ++index) {
      const auto key = static_cast<Key>(index);
      if (m_Impl->window.Input().IsDown(key)) {
        dispatch(KeyAction::Held, KeyEvent{key, modifiers, dt});
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
        dispatch(MouseAction::Held, MouseEvent{button, modifiers, dt});
      }
      if (m_Impl->stopRequested || m_Impl->window.ShouldClose()) {
        break;
      }
    }
    if (m_Impl->stopRequested || m_Impl->window.ShouldClose()) {
      break;
    }
    dispatch(FrameUpdate{dt, std::chrono::duration<double>(now - start).count(), frames + 1});
    if (m_Impl->stopRequested || m_Impl->window.ShouldClose()) {
      break;
    }

    m_Impl->runtime.tick(dt);
    m_Impl->runtime.render({size.width, size.height});
    const auto completed = Clock::now();
    const auto stats = frameClock.FinishFrame(completed);
    frames = stats.frame;
    if (m_Impl->logPerformance) {
      const auto sample = diagnostics.FinishFrame(now, completed);
      frameLog << Log::trace("frame={} interval={:.2f}ms work={:.2f}ms", frames, sample.intervalMs,
                             sample.workMs);
      if (sample.hitch) {
        performanceLog << Log::warn("Frame hitch: frame={} interval={:.2f}ms budget={:.2f}ms", frames,
                                    sample.intervalMs, 1000.0 / m_Impl->frameLimits);
      }
      if (sample.summary) {
        const auto &summary = *sample.summary;
        performanceLog << Log::debug(
            "fps={:.1f} interval_avg={:.2f}ms interval_max={:.2f}ms work_avg={:.2f}ms work_max={:.2f}ms",
            summary.fps, summary.intervalAvgMs, summary.intervalMaxMs, summary.workAvgMs, summary.workMaxMs);
        const auto progress = runtime().renderer().progress();
        if (progress.samplesPerPixel && *progress.samplesPerPixel != 0)
          rayLog << Log::debug("revision={} spp={} size={}x{} sample_ms={:.2f}", progress.revision,
                               *progress.samplesPerPixel, progress.size.width, progress.size.height,
                               progress.sampleMilliseconds);
      }
    }
    if (options.frameLimit != 0 && frames >= options.frameLimit) {
      break;
    }
  }

  m_Impl->runtime.finish();

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
