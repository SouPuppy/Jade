#pragma once
#include <Jade/Renderer/RayTracing/RayTracing.hpp>
#include <Jade/Renderer/RayTracing/ComputeRayTracer.hpp>
#include <Jade/Renderer/Common/SceneSnapshot.hpp>
#include <memory>
#include <chrono>
#include <string>

namespace Jade { class Runtime; class Window; namespace Detail { struct CollisionDebugLines; } }
namespace Jade::Renderer {
struct Config {
  RayTracingSettings rayTracing;
  bool vsync = false;
  bool wireframe = false;
};
class System final {
public:
  ~System();
  System(const System&) = delete;
  System& operator=(const System&) = delete;
  RayTracing& rayTracing() noexcept;
  RayTracingSettings rayTracingSettings() const;
  // Call on the application/window thread, including from FrameUpdate.
  // Exposure/maxSamples retain Film. Algorithm/sampling changes reset accumulation.
  void setRayTracingSettings(const RayTracingSettings&);
  RenderProgress progress() const;
  void setRayTracingEnabled(bool enabled) noexcept;
  bool rayTracingEnabled() const noexcept;
  void setWireframe(bool enabled) noexcept;
  bool wireframeEnabled() const noexcept;
private:
  friend class Jade::Runtime;
  System(Window&, JobSystem&, Config, std::string title);
  void start();
  void stop() noexcept;
  void invalidate() noexcept;
  void rethrowError();
  void render(const SceneSnapshot*, ImageSize, bool axes, const Detail::CollisionDebugLines*, bool paused,
              std::chrono::steady_clock::time_point deadline);
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Jade::Renderer
