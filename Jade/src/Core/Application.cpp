#include "Application.hpp"

#include "../internal/Core/FrameClock.hpp"
#include "../internal/Math/Validation.hpp"
#include "../internal/Renderer/RenderSystem.hpp"

#include <Jade/Log.hpp>

#include <cmath>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace Jade {

Application::Application(const ApplicationConfig& config)
{
  if (config.frameLimits == 0) {
    throw std::invalid_argument("Frame limits must be positive.");
  }
  Detail::Validate(config.clearColor);
  m_Impl = std::make_unique<Impl>(config);
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
  struct RunGuard {
    bool& running;
    ~RunGuard() { running = false; }
  } guard{m_Impl->running};

  using Clock = Detail::FrameClock::Clock;
  const auto start = Clock::now();
  Detail::FrameClock frameClock(m_Impl->frameLimits, start);
  std::optional<Clock::time_point> previousFrame;
  std::uint64_t frames = 0;

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
          Log::Info(axisGizmoEnabled() ? "Axis Gizmo: on" : "Axis Gizmo: off");
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
    const auto stats = frameClock.FinishFrame(Clock::now());
    frames = stats.frame;
    if (m_Impl->logFrames) {
      std::ostringstream message;
      message.imbue(std::locale::classic());
      message << std::fixed << std::setprecision(3)
        << "elapsed=" << stats.elapsed << "s second=" << stats.second
        << " frame=" << stats.frame << " frame_in_second=" << stats.frameInSecond
        << std::setprecision(1) << " fps=" << stats.fps
        << " frame_limits=" << m_Impl->frameLimits;
      Log::Info(message.str());
    }
    if (options.frameLimit != 0 && frames >= options.frameLimit) {
      break;
    }
  }

  if (options.frameLimit != 0 && frames != options.frameLimit && !m_Impl->stopRequested) {
    throw std::runtime_error("Application closed before the frame limit.");
  }
}

} // namespace Jade
