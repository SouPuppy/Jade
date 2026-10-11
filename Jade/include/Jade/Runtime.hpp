#pragma once
#include <Jade/EngineConfig.hpp>
#include <Jade/Assets/Assets.hpp>
#include <memory>

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
  Renderer::System& renderer() noexcept;
  Physics::System& physics() noexcept;
  JobSystem& jobs() noexcept;
  // Accumulates display dt and executes fixed simulation steps only.
  void tick(float dt);
  unsigned substeps() const noexcept;
  double droppedTime() const noexcept;
  void setPaused(bool paused) noexcept;
  bool paused() const noexcept;
  void setAxes(bool enabled) noexcept;
  bool axesEnabled() const noexcept;
  void setPhysicsDebug(bool enabled) noexcept;
  bool physicsDebugEnabled() const noexcept;
private:
  friend class Application;
  Runtime(Window&, const EngineConfig&);
  bool commitScene();
  void begin();
  void end() noexcept;
  void abort() noexcept;
  void finish();
  void suspend() noexcept;
  void render(Renderer::ImageSize);
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Jade
