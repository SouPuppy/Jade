#pragma once

#include <Jade/WindowConfig.hpp>

#include "GLFW/GlfwRuntime.hpp"
#include "GLFW/GlfwWindow.hpp"
#include "../Input/InputState.hpp"

#include <memory>

namespace Jade {

struct FramebufferSize {
  int width = 0;
  int height = 0;
};

namespace Detail { class OpenGLDevice; }

// Internal window facade. All operations belong to the main thread.
class Window final {
public:
  explicit Window(const WindowConfig& config);
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  void PollEvents();
  void WaitEvents(double timeoutSeconds);
  bool ShouldClose() const;
  void RequestClose();
  void SetTitle(const std::string& title);
  void SetMouseCaptured(bool captured);
  bool IsKeyPressed(Key key) const;
  Detail::InputState& Input() noexcept;
  FramebufferSize GetFramebufferSize() const;

private:
  friend class Detail::OpenGLDevice;
  void* NativeHandle() const;

  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

struct Window::Impl {
  Detail::GlfwRuntime runtime;
  Detail::InputState input;
  Detail::GlfwWindow window;

  explicit Impl(const WindowConfig& config)
    : window(config, Detail::WindowClientApi::OpenGL)
  {
  }
};

} // namespace Jade
