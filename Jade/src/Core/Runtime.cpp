#include <Jade/Runtime.hpp>
#include "Scene.hpp"
#include "../internal/Platform/Window.hpp"
#include "../internal/Renderer/Debug/CollisionDebug.hpp"
#include <Jade/Log.hpp>
#include <Jade/Core/Transform.hpp>
#include <Jade/Physics/Collider.hpp>
#include <cmath>
#include <optional>

namespace Jade {
namespace {
const Log::Logger debugLog{"Physics/Debug"};
const Log::Logger physicsLog{"Physics"};
const Log::Logger sceneLog{"Scene"};
}
struct Runtime::Impl {
  Window& window;
  JobSystem jobs;
  Assets assets;
  std::shared_ptr<Scene> scene;
  std::optional<std::shared_ptr<Scene>> pending;
  Renderer::System renderer;
  Physics::System physics;
  bool running = false, switching = false, paused = false, axes, aabbs, physicsDebug, logNow = false;
  double accumulator = 0, dropped = 0, logElapsed = 0;
  unsigned substeps = 0;
  Impl(Window& w, const EngineConfig& c)
    : window(w), jobs(c.jobs), renderer(w, jobs, c.renderer, c.window.title), physics(jobs, c.physics), axes(c.debug.axes),
      aabbs(c.debug.aabbs), physicsDebug(c.debug.physics), logNow(c.debug.physics) {}
};
Runtime::Runtime(Window& window, const EngineConfig& config) : m_Impl(std::make_unique<Impl>(window, config)) {}
Runtime::~Runtime() {
  m_Impl->renderer.stop();
  m_Impl->jobs.shutdown();
  if (m_Impl->scene) m_Impl->scene->m_Impl->owner = nullptr;
}
Assets& Runtime::assets() noexcept { return m_Impl->assets; }
std::shared_ptr<Scene> Runtime::scene() const noexcept { return m_Impl->scene; }
Renderer::System& Runtime::renderer() noexcept { return m_Impl->renderer; }
Physics::System& Runtime::physics() noexcept { return m_Impl->physics; }
JobSystem& Runtime::jobs() noexcept { return m_Impl->jobs; }
void Runtime::setScene(std::shared_ptr<Scene> scene) {
  if (scene && scene->m_Impl->owner && scene->m_Impl->owner != this)
    throw std::invalid_argument("Scene is already active in another Runtime.");
  m_Impl->pending = std::move(scene);
  if (!m_Impl->running && !m_Impl->switching) commitScene();
}
bool Runtime::commitScene() {
  if (!m_Impl->pending || m_Impl->switching) return false;
  auto next = std::move(*m_Impl->pending); m_Impl->pending.reset();
  const auto previous = m_Impl->scene;
  if (next == previous) return false;
  if (next && next->m_Impl->owner && next->m_Impl->owner != this)
    throw std::invalid_argument("Scene is already active in another Runtime.");
  if (next) next->m_Impl->owner = this;
  m_Impl->switching = true;
  struct Guard { bool& flag; ~Guard() { flag = false; } } guard{m_Impl->switching};
  try {
    if (previous) previous->m_Impl->events.DispatchLifecycle(false);
    m_Impl->scene = next;
    m_Impl->window.SetMouseCaptured(next && next->CapturesMouse());
    m_Impl->window.Input().ResetCursor();
    suspend(); m_Impl->renderer.invalidate(); m_Impl->logNow = true;
    if (previous) { previous->m_Impl->owner = nullptr; sceneLog << Log::debug("Deactivated: scene={}", previous->m_Impl->id); }
    if (next) { sceneLog << Log::debug("Activated: scene={}", next->m_Impl->id); next->m_Impl->events.DispatchLifecycle(true); }
  } catch (...) {
    if (next && m_Impl->scene != next) next->m_Impl->owner = nullptr;
    m_Impl->pending.reset(); throw;
  }
  return true;
}
void Runtime::begin() {
  if (m_Impl->running || m_Impl->switching) throw std::logic_error("Runtime is already active.");
  suspend(); m_Impl->renderer.start(); m_Impl->running = true;
}
void Runtime::end() noexcept {
  m_Impl->renderer.stop(); m_Impl->running = false; suspend();
}
void Runtime::finish() { end(); m_Impl->renderer.rethrowError(); }
void Runtime::abort() noexcept { m_Impl->pending.reset(); end(); }
void Runtime::suspend() noexcept {
  m_Impl->accumulator = 0; m_Impl->substeps = 0; m_Impl->logElapsed = 0;
  m_Impl->physics.reset();
}
void Runtime::tick(float dt) {
  if (!std::isfinite(dt) || dt < 0) throw std::invalid_argument("Runtime dt must be finite and nonnegative.");
  m_Impl->substeps = 0;
  if (!m_Impl->scene || m_Impl->paused) { m_Impl->accumulator = 0; return; }
  m_Impl->logElapsed += dt;
  const auto& config = m_Impl->physics.config();
  m_Impl->accumulator += dt;
  while (m_Impl->accumulator + 1e-9 >= config.fixedStep && m_Impl->substeps < config.maxSubsteps) {
    m_Impl->physics.step(*m_Impl->scene, static_cast<float>(config.fixedStep));
    m_Impl->accumulator -= config.fixedStep;
    m_Impl->accumulator = std::max(0.0, m_Impl->accumulator);
    ++m_Impl->substeps;
  }
  if (m_Impl->accumulator >= config.fixedStep) {
    const double remainder = std::fmod(m_Impl->accumulator, config.fixedStep);
    const double discarded = m_Impl->accumulator - remainder;
    m_Impl->dropped += discarded; m_Impl->accumulator = remainder;
    physicsLog << Log::warn("Substep limit: discarded={:.6f}s total={:.6f}s", discarded, m_Impl->dropped);
  }
}
unsigned Runtime::substeps() const noexcept { return m_Impl->substeps; }
double Runtime::droppedTime() const noexcept { return m_Impl->dropped; }
void Runtime::setPaused(bool paused) noexcept {
  if (paused != m_Impl->paused) {
    m_Impl->paused = paused; m_Impl->accumulator = 0; m_Impl->substeps = 0;
    m_Impl->logNow = true;
  }
}
bool Runtime::paused() const noexcept { return m_Impl->paused; }
void Runtime::setAxes(bool enabled) noexcept { m_Impl->axes = enabled; }
bool Runtime::axesEnabled() const noexcept { return m_Impl->axes; }
void Runtime::setAabbs(bool enabled) noexcept { m_Impl->aabbs = enabled; }
bool Runtime::aabbsEnabled() const noexcept { return m_Impl->aabbs; }
void Runtime::setPhysicsDebug(bool enabled) noexcept {
  if (enabled != m_Impl->physicsDebug) {
    m_Impl->physicsDebug = enabled; m_Impl->logNow = enabled; m_Impl->logElapsed = 0;
    debugLog << Log::info("Physics Debug: {}", enabled ? "on" : "off");
  }
}
bool Runtime::physicsDebugEnabled() const noexcept { return m_Impl->physicsDebug; }
void Runtime::render(Renderer::ImageSize size) {
  m_Impl->renderer.rethrowError();
  std::optional<Renderer::SceneSnapshot> snapshot;
  std::optional<Detail::CollisionDebugLines> debug;
  if (m_Impl->scene) {
    const auto camera = m_Impl->scene->camera();
    const bool geometry = m_Impl->renderer.renderEnabled() || m_Impl->renderer.wireframeEnabled();
    if ((geometry || m_Impl->axes || m_Impl->aabbs || m_Impl->physicsDebug) && camera.has<Transform>() && camera.has<Camera>())
      snapshot = Renderer::SceneSnapshot::capture(*m_Impl->scene, m_Impl->assets, geometry);
    if (m_Impl->physicsDebug || m_Impl->aabbs)
      debug = Detail::captureCollisionDebug(*m_Impl->scene, m_Impl->physicsDebug, m_Impl->aabbs,
        m_Impl->physicsDebug, &m_Impl->physics, m_Impl->physicsDebug);
    if (m_Impl->physicsDebug) {
      if (m_Impl->logNow || m_Impl->logElapsed >= 1) {
        m_Impl->logNow = false; m_Impl->logElapsed = std::fmod(m_Impl->logElapsed, 1.0);
        const auto& c = m_Impl->physics.config();
        std::size_t dynamic = 0, statics = 0, count = 0; float penetration = 0;
        for (const auto entity : m_Impl->scene->query<Transform, Collider>()) {
          if (entity.get<Collider>().empty()) continue;
          if (entity.has<RigidBody>() && entity.get<RigidBody>().mass > 0) ++dynamic; else ++statics;
        }
        for (const auto& manifold : debug->contacts) {
          count += manifold.points.size();
          for (const auto& p : manifold.points) penetration = std::max(penetration, -p.separation);
        }
        debugLog << Log::debug("fields={} integration={} fixed_step={:.6f}s substeps={} paused={} dynamic={} static={} contacts={} max_penetration={:.6f}m dropped={:.6f}s",
          m_Impl->scene->physics().fields.size(),
          m_Impl->scene->physics().integration == Physics::Integration::VelocityVerlet ? "VelocityVerlet" : "SemiImplicitEuler",
          c.fixedStep, m_Impl->substeps, m_Impl->paused, dynamic, statics, count, penetration, m_Impl->dropped);
        for (const auto entity : m_Impl->scene->query<Transform, Collider, RigidBody>()) {
          if (entity.get<Collider>().empty()) continue;
          const auto& b = entity.get<RigidBody>();
          debugLog << Log::debug("entity={} mass={} velocity=({:.4f},{:.4f},{:.4f}) angular_velocity=({:.4f},{:.4f},{:.4f})",
            entity.id(), b.mass, b.velocity.x, b.velocity.y, b.velocity.z, b.angularVelocity.x, b.angularVelocity.y, b.angularVelocity.z);
        }
        for (const auto& f : m_Impl->physics.forceSnapshot(*m_Impl->scene))
          debugLog << Log::debug("entity={} field_force=({:.4f},{:.4f},{:.4f}) contact_force=({:.4f},{:.4f},{:.4f}) total_force=({:.4f},{:.4f},{:.4f}) solved={}",
            f.entity.id(), f.fieldForce.x, f.fieldForce.y, f.fieldForce.z,
            f.contactForce.x, f.contactForce.y, f.contactForce.z,
            f.totalForce.x, f.totalForce.y, f.totalForce.z, f.solved);
      }
      if (Log::Enabled(Log::Level::Trace)) for (const auto& m : debug->contacts) for (const auto& p : m.points)
        debugLog << Log::trace("contact a={} b={} point=({:.4f},{:.4f},{:.4f}) normal=({:.3f},{:.3f},{:.3f}) penetration={:.6f} impulse={:.6f}",
          m.a.id(), m.b.id(), p.pointA.x, p.pointA.y, p.pointA.z, m.normal.x, m.normal.y, m.normal.z, std::max(0.0f, -p.separation), p.normalImpulse);
    }
  }
  m_Impl->renderer.render(snapshot ? &*snapshot : nullptr, size, m_Impl->axes, debug ? &*debug : nullptr);
}
} // namespace Jade
