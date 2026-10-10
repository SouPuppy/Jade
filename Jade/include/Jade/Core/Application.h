#pragma once

#include <Jade/Platform/Window.h>
#include <Jade/Renderer/Renderer.h>

#include <cstdint>
#include <memory>

namespace Jade {

struct ApplicationConfig {
  WindowConfig window;
  bool vsync = true;
  Color clearColor;
};

struct RunOptions {
  // Zero runs until the user closes the window.
  std::uint32_t frameLimit = 0;
  bool validateRendering = false;
};

class Application final {
public:
  explicit Application(const ApplicationConfig& config = {});
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  void Run(const RunOptions& options = {});

private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};

} // namespace Jade
