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
const Log::Logger sceneLog{"Scene"};
const Log::Logger frameLog{"Frame"};
const Log::Logger performanceLog{"Performance"};
const Log::Logger rayLog{"RayTracing"};

} // namespace

Application::Application(const ApplicationConfig &config)
{
  if (config.frameLimits == 0) {
    throw std::invalid_argument("Frame limits must be positive.");
  }
  m_Impl = std::make_unique<Impl>(config);
  applicationLog << Log::info("Ready: window={}x{} target_fps={} vsync={}", config.window.width,
                              config.window.height, config.frameLimits, config.vsync ? "on" : "off");
}

Application::~Application()
{
  m_Impl->rendering.stop();
  // Destruction releases ownership without invoking throwing user callbacks.
  if (m_Impl->scene)
    m_Impl->scene->m_Impl->owner = nullptr;
}

Assets &Application::assets() noexcept { return m_Impl->assets; }
std::shared_ptr<Scene> Application::scene() const noexcept { return m_Impl->scene; }

void Application::InstallRenderer(std::unique_ptr<Renderer::Backend> backend)
{
  if (m_Impl->running || m_Impl->switching)
    throw std::logic_error("Renderer installation must occur outside Run and lifecycle callbacks.");
  m_Impl->rendering.install(std::move(backend));
}

Renderer::RenderProgress Application::renderProgress() const { return m_Impl->rendering.progress(); }

void Application::setScene(std::shared_ptr<Scene> scene)
{
  if (scene && scene->m_Impl->owner && scene->m_Impl->owner != this) {
    throw std::invalid_argument("Scene is already active in another Application.");
  }
  m_Impl->pendingScene = std::move(scene);
  if (!m_Impl->running && !m_Impl->switching)
    ApplyPendingScene();
}

bool Application::ApplyPendingScene()
{
  if (!m_Impl->pendingScene || m_Impl->switching)
    return false;
  auto next = std::move(*m_Impl->pendingScene);
  m_Impl->pendingScene.reset();
  const auto previous = m_Impl->scene;
  if (previous == next)
    return false;
  if (next && next->m_Impl->owner && next->m_Impl->owner != this) {
    throw std::invalid_argument("Scene is already active in another Application.");
  }
  if (next)
    next->m_Impl->owner = this; // Reserve through reentrant lifecycle callbacks.
  m_Impl->switching = true;
  struct SwitchGuard {
    bool &switching;
    ~SwitchGuard() { switching = false; }
  } guard{m_Impl->switching};
  try {
    if (previous) {
      previous->m_Impl->events.DispatchLifecycle(false);
    }
    m_Impl->scene = next;
    m_Impl->rendering.invalidate();
    if (previous) {
      previous->m_Impl->owner = nullptr;
      sceneLog << Log::debug("Deactivated: scene={}", previous->m_Impl->id);
    }
    if (next) {
      sceneLog << Log::debug("Activated: scene={}", next->m_Impl->id);
      next->m_Impl->events.DispatchLifecycle(true);
    }
  } catch (...) {
    if (next && m_Impl->scene != next)
      next->m_Impl->owner = nullptr;
    m_Impl->pendingScene.reset();
    throw;
  }
  return true;
}

void Application::setWireframe(bool enabled) noexcept
{
  if (m_Impl->wireframe != enabled) {
    m_Impl->wireframe = enabled;
    m_Impl->rendering.invalidate();
  }
}

bool Application::wireframeEnabled() const noexcept { return m_Impl->wireframe; }

void Application::setAxisGizmo(bool enabled) noexcept { m_Impl->axisGizmo = enabled; }

bool Application::axisGizmoEnabled() const noexcept { return m_Impl->axisGizmo; }

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
  if (m_Impl->running || m_Impl->switching) {
    throw std::logic_error("Application::Run cannot be called from an event callback.");
  }
  m_Impl->rendering.start();
  m_Impl->running = true;
  m_Impl->stopRequested = false;
  using Clock = Detail::FrameClock::Clock;
  const auto start = Clock::now();
  std::uint64_t frames = 0;
  struct RunGuard {
    Detail::RenderSession &rendering;
    bool &running;
    const std::uint64_t &frames;
    Clock::time_point start;
    std::optional<std::shared_ptr<Scene>> &pending;
    std::string_view reason = "window_closed";
    int exceptions = std::uncaught_exceptions();
    ~RunGuard()
    {
      rendering.stop();
      running = false;
      const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
      if (std::uncaught_exceptions() > exceptions) {
        pending.reset();
        applicationLog << Log::error("Run aborted by exception: frames={} elapsed={:.2f}s", frames, elapsed);
      } else {
        applicationLog << Log::info("Run stopped: frames={} elapsed={:.2f}s reason={}", frames, elapsed,
                                    reason);
      }
    }
  } guard{m_Impl->rendering, m_Impl->running, frames, start, m_Impl->pendingScene};

  applicationLog << Log::info("Run started");
  Detail::FrameClock frameClock(m_Impl->frameLimits, start);
  Detail::FrameDiagnostics diagnostics(m_Impl->frameLimits);
  std::optional<Clock::time_point> previousFrame;
  Renderer::ImageSize previousSize;

  while (!m_Impl->window.ShouldClose() && !m_Impl->stopRequested) {
    if (ApplyPendingScene()) {
      previousFrame.reset();
      diagnostics.Resume();
      frameClock.Resume(Clock::now());
    }
    if (m_Impl->stopRequested)
      break;
    // One processing pass retains the same scene through input, update and present.
    const auto active = m_Impl->scene;
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
        if (m_Impl->debug && key->action == KeyAction::Pressed && key->event.modifiers == Modifiers::None &&
            flow == EventFlow::Continue && !m_Impl->stopRequested && !m_Impl->window.ShouldClose()) {
          if (key->event.key == Key::F) {
            setWireframe(!wireframeEnabled());
            applicationLog << Log::info("Wireframe: {}", wireframeEnabled() ? "on" : "off");
          } else if (key->event.key == Key::F3) {
            setAxisGizmo(!axisGizmoEnabled());
            applicationLog << Log::info("Axis Gizmo: {}", axisGizmoEnabled() ? "on" : "off");
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
      m_Impl->rendering.invalidate();
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

    const Renderer::ImageSize imageSize{size.width, size.height};
    if (imageSize != previousSize) {
      m_Impl->rendering.invalidate();
      previousSize = imageSize;
    }
    m_Impl->rendering.rethrowError();
    m_Impl->display.begin(imageSize);
    if (active) {
      const auto camera = active->camera();
      if (camera.has<Transform>() && camera.has<Camera>()) {
        auto snapshot = Renderer::SceneSnapshot::capture(*active, m_Impl->assets);
        if (m_Impl->wireframe) {
          m_Impl->display.wireframe(snapshot, imageSize);
        } else {
          m_Impl->rendering.submit(snapshot, imageSize);
          if (const auto image = m_Impl->rendering.image())
            m_Impl->display.image(*image);
        }
        if (m_Impl->axisGizmo)
          m_Impl->display.axes(*snapshot.camera, imageSize);
      } else
        m_Impl->rendering.invalidate();
    } else
      m_Impl->rendering.invalidate();
    m_Impl->display.present();
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
        const auto progress = renderProgress();
        if (progress.samplesPerPixel != 0)
          rayLog << Log::debug("revision={} spp={} size={}x{} sample_ms={:.2f}", progress.revision,
                               progress.samplesPerPixel, progress.size.width, progress.size.height,
                               progress.sampleMilliseconds);
      }
    }
    if (options.frameLimit != 0 && frames >= options.frameLimit) {
      break;
    }
  }

  m_Impl->rendering.stop();
  m_Impl->rendering.rethrowError();

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
