#pragma once

#include <memory>
#include <string>

namespace Jade {

struct WindowConfig {
  std::string title = "Jade";
  int width = 960;
  int height = 640;
  bool visible = true;
};

struct FramebufferSize {
  int width = 0;
  int height = 0;
};

enum class Key {
  Escape
};

class Renderer;

// Window and rendering operations must run on the main thread.
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
  bool IsKeyPressed(Key key) const;
  FramebufferSize GetFramebufferSize() const;

private:
  friend class Renderer;
  void* NativeHandle() const;

  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade
