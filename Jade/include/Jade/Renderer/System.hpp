#pragma once
#include <Jade/Renderer/RayTracing/RayTracing.hpp>
#include <Jade/Renderer/Common/SceneSnapshot.hpp>
#include <memory>
#include <string>

namespace Jade { class Runtime; class Window; namespace Detail { struct CollisionDebugLines; } }
namespace Jade::Renderer {
enum class Mode { RayTracing, Wireframe };
struct Config {
  RayTracingConfig rayTracing;
  bool vsync = false;
  Mode mode = Mode::RayTracing;
};
class System final {
public:
  ~System();
  System(const System&) = delete;
  System& operator=(const System&) = delete;
  RayTracing& rayTracing() noexcept;
  RenderProgress progress() const;
  void setMode(Mode mode) noexcept;
  Mode mode() const noexcept;
private:
  friend class Jade::Runtime;
  System(Window&, Config, std::string title);
  void start();
  void stop() noexcept;
  void invalidate() noexcept;
  void rethrowError();
  void render(const SceneSnapshot*, ImageSize, bool axes, const Detail::CollisionDebugLines*);
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Jade::Renderer
