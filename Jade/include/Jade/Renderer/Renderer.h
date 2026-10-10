#pragma once

#include <Jade/Platform/Window.h>

#include <memory>

namespace Jade {

struct Color {
  float red = 0.08f;
  float green = 0.10f;
  float blue = 0.14f;
  float alpha = 1.0f;
};

// A small rendering facade, not a cross-API RHI. The window must outlive it.
class Renderer final {
public:
  Renderer(Window& window, bool vsync);
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  bool BeginFrame(FramebufferSize size, const Color& clearColor);
  void Present();
  bool ValidateClear(const Color& expectedColor) const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade
