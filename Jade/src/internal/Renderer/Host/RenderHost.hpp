#pragma once
#include "ProgressiveSession.hpp"
#include "../Device/OpenGL/OpenGLDevice.hpp"
#include "../Presentation/OpenGLDisplay.hpp"
#include "../Debug/DebugDraw.hpp"
#include <Jade/Renderer/RayTracing/RayTracing.hpp>

namespace Jade::Detail {
class RenderHost final {
public:
  RenderHost(Window &, bool vsync, std::string title, Renderer::RayTracingConfig);
  ~RenderHost();
  Renderer::RayTracing &rayTracing() noexcept { return m_RayTracing; }
  void start(bool wireframe);
  void stop() noexcept;
  void invalidate() noexcept;
  void rethrowError();
  Renderer::RenderProgress progress() const;
  void draw(const Renderer::SceneSnapshot *, Renderer::ImageSize, bool wireframe, bool axes);

private:
  Window &m_Window;
  std::string m_Title;
  OpenGLDevice m_Device;
  OpenGLDisplay m_Display;
  DebugDraw m_Debug;
  Renderer::RayTracing m_RayTracing;
  ProgressiveSession m_Session;
  bool m_Wireframe = false;
  Renderer::ImageSize m_FrameSize;
};
} // namespace Jade::Detail
