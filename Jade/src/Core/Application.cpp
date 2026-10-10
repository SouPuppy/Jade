#include <Jade/Core/Application.h>

#include <stdexcept>

namespace Jade {

struct Application::Impl {
  Color clearColor;
  Window window;
  Renderer renderer;

  explicit Impl(const ApplicationConfig& config)
    : clearColor(config.clearColor),
      window(config.window),
      renderer(window, config.vsync)
  {
  }
};

Application::Application(const ApplicationConfig& config)
  : m_Impl(std::make_unique<Impl>(config))
{
}

Application::~Application() = default;

void Application::Run(const RunOptions& options)
{
  std::uint32_t frames = 0;

  while (!m_Impl->window.ShouldClose()) {
    m_Impl->window.PollEvents();
    if (m_Impl->window.IsKeyPressed(Key::Escape)) {
      m_Impl->window.RequestClose();
    }
    if (m_Impl->window.ShouldClose()) {
      break;
    }

    if (!m_Impl->renderer.BeginFrame(
        m_Impl->window.GetFramebufferSize(), m_Impl->clearColor)) {
      m_Impl->window.WaitEvents(0.05);
      continue;
    }

    if (options.validateRendering &&
      !m_Impl->renderer.ValidateClear(m_Impl->clearColor)) {
      throw std::runtime_error("Framebuffer clear validation failed.");
    }

    m_Impl->renderer.Present();
    if (options.frameLimit != 0 && ++frames >= options.frameLimit) {
      break;
    }
  }

  if (options.frameLimit != 0 && frames != options.frameLimit) {
    throw std::runtime_error("Application closed before the frame limit.");
  }
}

} // namespace Jade
