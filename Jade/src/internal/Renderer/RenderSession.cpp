#include "RenderSession.hpp"

#include <chrono>
#include <limits>

namespace Jade::Detail {

RenderSession::~RenderSession() { stop(); }
void RenderSession::install(std::unique_ptr<Renderer::Backend> backend)
{
  if (!backend)
    throw std::invalid_argument("Renderer cannot be null.");
  const auto settings = backend->settings();
  if (!std::isfinite(settings.resolutionScale) || settings.resolutionScale <= 0 ||
      settings.resolutionScale > 8 || !std::isfinite(settings.exposureEV) ||
      std::abs(settings.exposureEV) > 64)
    throw std::invalid_argument("Invalid renderer resolution scale or exposure.");
  stop();
  m_Backend = std::move(backend);
  m_Settings = settings;
  {
    std::lock_guard lock(m_Mutex);
    m_Error = nullptr;
  }
  invalidate();
}
void RenderSession::start()
{
  if (!m_Backend)
    throw std::logic_error("Install a renderer with Application::use before Run.");
  stop();
  invalidate();
  m_Backend->m_SessionActive = true;
  try {
    m_Thread = std::jthread([this](std::stop_token stop) { run(stop); });
  } catch (...) {
    m_Backend->m_SessionActive = false;
    throw;
  }
}
void RenderSession::stop() noexcept
{
  {
    std::lock_guard lock(m_Mutex);
    m_Thread.request_stop();
    m_Cancel.request_stop();
    m_Pending.reset();
  }
  m_Ready.notify_all();
  if (m_Thread.joinable())
    m_Thread.join();
  if (m_Backend)
    m_Backend->m_SessionActive = false;
  {
    std::lock_guard lock(m_Mutex);
    m_Progress.inFlight = false;
  }
}
void RenderSession::invalidate() noexcept
{
  std::lock_guard lock(m_Mutex);
  ++m_Epoch;
  m_Cancel.request_stop();
  m_Pending.reset();
  m_LastInput.reset();
  m_Image.reset();
  m_Progress = {};
  m_Ready.notify_all();
}
void RenderSession::submit(Renderer::SceneSnapshot snapshot, Renderer::ImageSize framebuffer)
{
  if (!snapshot.camera) {
    invalidate();
    return;
  }
  const double width = framebuffer.width * static_cast<double>(m_Settings.resolutionScale);
  const double height = framebuffer.height * static_cast<double>(m_Settings.resolutionScale);
  if (width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max())
    throw std::invalid_argument("Scaled framebuffer exceeds supported dimensions.");
  const Renderer::ImageSize size{std::max(1, static_cast<int>(std::lround(width))),
                                 std::max(1, static_cast<int>(std::lround(height)))};
  (void)size.pixels();
  std::lock_guard lock(m_Mutex);
  if (m_LastInput && m_LastInput->size == size && m_LastInput->snapshot == snapshot)
    return;
  if (m_LastInput && m_LastInput->size != size) {
    ++m_Epoch;
    m_Cancel.request_stop();
    m_Image.reset();
    m_Progress = {};
  }
  auto input = std::make_shared<Input>(Input{std::move(snapshot), size, m_Epoch, ++m_Revision});
  m_Pending = input;
  m_LastInput = std::move(input);
  m_Ready.notify_one();
}
std::shared_ptr<const Renderer::Image> RenderSession::image() const
{
  std::lock_guard lock(m_Mutex);
  return m_Image;
}
Renderer::RenderProgress RenderSession::progress() const
{
  std::lock_guard lock(m_Mutex);
  return m_Progress;
}
void RenderSession::rethrowError()
{
  std::exception_ptr error;
  {
    std::lock_guard lock(m_Mutex);
    error = std::exchange(m_Error, nullptr);
  }
  if (error)
    std::rethrow_exception(error);
}

void RenderSession::run(std::stop_token stop)
{
  std::shared_ptr<const Input> current;
  std::unique_ptr<Renderer::Film> film;
  Renderer::RenderScene scene;
  try {
    for (;;) {
      bool changed = false;
      std::unique_lock lock(m_Mutex);
      m_Ready.wait(lock, [&] {
        return stop.stop_requested() || m_Pending ||
               (current && current->epoch == m_Epoch &&
                (m_Settings.maxSamples == 0 || film->samples() < m_Settings.maxSamples));
      });
      if (stop.stop_requested())
        return;
      if (current && current->epoch != m_Epoch) {
        current.reset();
        film.reset();
      }
      if (m_Pending) {
        current = std::exchange(m_Pending, nullptr);
        changed = true;
      }
      if (!current)
        continue;
      m_Cancel = std::stop_source();
      const auto token = m_Cancel.get_token();
      m_Progress.inFlight = true;
      lock.unlock();
      if (changed) {
        scene = m_Compiler.compile(current->snapshot, token);
        film = std::make_unique<Renderer::Film>(current->size);
      }
      const auto start = std::chrono::steady_clock::now();
      const Renderer::RenderRequest request{*current->snapshot.camera, current->size, film->samples(),
                                            m_Settings.seed, token};
      m_Backend->render(scene, request, *film);
      if (token.stop_requested())
        continue;
      if (film->samples() != request.sampleIndex + 1)
        throw std::logic_error("Backend must commit exactly one sample round.");
      auto image = std::make_shared<Renderer::Image>(film->resolve(m_Settings.exposureEV));
      const double milliseconds =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      lock.lock();
      if (current->epoch == m_Epoch && !stop.stop_requested()) {
        m_Image = std::move(image);
        m_Progress = {current->revision, film->samples(), current->size, milliseconds, false};
      }
    }
  } catch (...) {
    std::lock_guard lock(m_Mutex);
    m_Error = std::current_exception();
    m_Progress.inFlight = false;
  }
}

} // namespace Jade::Detail
