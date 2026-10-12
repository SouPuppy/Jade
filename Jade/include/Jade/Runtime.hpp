#pragma once
#include <Jade/EngineConfig.hpp>
#include <Jade/Assets/Assets.hpp>
#include <Jade/Animation/AnimationSystem.hpp>
#include <Jade/Renderer/RayTracing/Integrator.hpp>
#include <Jade/Events/Keyboard.hpp>
#include <Jade/Events/Mouse.hpp>
#include <Jade/Events/MouseMoved.hpp>
#include <Jade/Events/MouseScrolled.hpp>
#include <Jade/Graphics/CameraControl.hpp>
#include <memory>
#include <chrono>

namespace Jade {
class Application;
class Runtime final {
public:
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Assets& assets() noexcept;
  std::shared_ptr<Scene> scene() const noexcept;
  void setScene(std::shared_ptr<Scene> scene);
  void addWorld(std::shared_ptr<World> world);
  void removeWorld(const std::shared_ptr<World>& world);
  Renderer::System& renderer() noexcept;
  Physics::System& physics() noexcept;
  Animation::System& animation() noexcept;
  JobSystem& jobs() noexcept;
  // Executes fixed physics steps, then advances sprite animation before rendering.
  void tick(float dt);
  unsigned substeps() const noexcept;
  double droppedTime() const noexcept;
  void setPaused(bool paused) noexcept;
  bool paused() const noexcept;
  void setAxes(bool enabled) noexcept;
  bool axesEnabled() const noexcept;
  void setAabbs(bool enabled) noexcept;
  bool aabbsEnabled() const noexcept;
  void setPhysicsDebug(bool enabled) noexcept;
  bool physicsDebugEnabled() const noexcept;
  void setSoulMode(bool);
  bool soulMode() const noexcept;
  const Renderer::CameraFrame* soulCamera() const noexcept;
  const std::vector<Renderer::PathSegment>& soulPaths() const noexcept;
  // Observer and scene cameras use the same control settings and navigation.
  CameraControl& observerControls() noexcept;
  const CameraControl& observerControls() const noexcept;
private:
  friend class Application;
  Runtime(Window&, const EngineConfig&);
  bool commitScene();
  bool soulKey(KeyAction,const KeyEvent&);
  bool soulMouse(MouseAction,const MouseEvent&);
  bool soulMouseMoved(const MouseMoved&);
  bool soulScrolled(const MouseScrolled&);
  bool capturesMouse() const noexcept;
  void updateObserver(float dt);
  void beginCameraFrame() noexcept;
  void toggleProjection();
  void begin();
  void end() noexcept;
  void abort() noexcept;
  void finish();
  void suspend() noexcept;
  void render(Renderer::ImageSize, std::chrono::steady_clock::time_point deadline);
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Jade
