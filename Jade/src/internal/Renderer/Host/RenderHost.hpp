#pragma once
#include "ProgressiveSession.hpp"
#include "../Device/OpenGL/OpenGLDevice.hpp"
#include "../Presentation/OpenGLDisplay.hpp"
#include "../Debug/DebugDraw.hpp"
#include <optional>
#include <typeindex>
namespace Jade::Detail {
class RenderHost final {
public:
  RenderHost(Window&, bool vsync, std::string title);
  ~RenderHost();
  void install(std::type_index, std::unique_ptr<Renderer::Backend>);
  void select(std::type_index);
  void cycle();
  const Renderer::Backend* active() const noexcept;
  void applyPending();
  void start();
  void stop() noexcept;
  void invalidate() noexcept;
  void rethrowError();
  Renderer::RenderProgress progress() const;
  void draw(const Renderer::SceneSnapshot*, Renderer::ImageSize, bool wireframe, bool axes);
private:
  struct Entry {
    std::type_index type;
    std::unique_ptr<Renderer::Backend> backend;
    std::unique_ptr<ProgressiveSession> session;
  };
  void activate();
  Window& m_Window;
  std::string m_Title;
  OpenGLDevice m_Device;
  OpenGLDisplay m_Display;
  DebugDraw m_Debug;
  OpenGLFrameTarget m_Target;
  std::vector<Entry> m_Entries;
  std::size_t m_Active=0;
  std::optional<std::size_t> m_Pending;
  std::optional<Renderer::SceneSnapshot> m_LastFrame;
  Renderer::RenderProgress m_Progress;
  bool m_Running=false,m_Wireframe=false,m_ProgressiveRunning=false;
  Renderer::ImageSize m_FrameSize;
  std::uint64_t m_Revision=0;
};
} // namespace Jade::Detail
