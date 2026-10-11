#include "ProgressiveSession.hpp"

#include <chrono>
#include <limits>

namespace Jade::Detail {

ProgressiveSession::~ProgressiveSession() { stop(); }
void ProgressiveSession::install(std::unique_ptr<Renderer::ProgressiveBackend> backend)
{
  if (!backend)
    throw std::invalid_argument("Renderer cannot be null.");
  const auto settings = backend->samplingSettings();
  if (!std::isfinite(settings.resolutionScale) || settings.resolutionScale <= 0 ||
      settings.resolutionScale > 8 || !std::isfinite(settings.exposureEV) ||
      std::abs(settings.exposureEV) > 64 || settings.minSamplesPerFrame == 0 || settings.minSamplesPerFrame > 1024)
    throw std::invalid_argument("Invalid renderer resolution scale or exposure.");
  stop();
  m_Owned = std::move(backend);
  m_Backend = m_Owned.get();
  m_Gpu = m_Backend->hasGpuFrames();
  m_Settings = settings;
  m_Encoding = m_Backend->imageEncoding();
  {
    std::lock_guard lock(m_Mutex);
    m_Error = nullptr;
  }
  invalidate();
}
void ProgressiveSession::bind(Renderer::ProgressiveBackend &backend)
{
  stop();
  m_Owned.reset();
  m_Backend = &backend;
  m_Gpu = backend.hasGpuFrames();
  m_Settings = backend.samplingSettings();
  m_Encoding = backend.imageEncoding();
  invalidate();
}
void ProgressiveSession::updateSettings(const Renderer::ProgressiveSettings& settings)
{
  if (!std::isfinite(settings.resolutionScale) || settings.resolutionScale <= 0 || settings.resolutionScale > 8 ||
      !std::isfinite(settings.exposureEV) || std::abs(settings.exposureEV) > 64 ||
      settings.minSamplesPerFrame == 0 || settings.minSamplesPerFrame > 1024)
    throw std::invalid_argument("Invalid renderer resolution scale or exposure.");
  std::lock_guard lock(m_Mutex);
  if (settings == m_Settings) return;
  const bool reset = settings.resolutionScale != m_Settings.resolutionScale || settings.seed != m_Settings.seed;
  m_Settings = settings;
  ++m_SettingsRevision;
  m_ResolvePending = true;
  if (reset) {
    ++m_Epoch;
    m_Cancel.request_stop();
    m_Pending.reset();
    m_LastInput.reset();
    m_Image.reset();
    m_GpuFrame.reset();
    m_Progress = {};
  }
  m_Ready.notify_one();
  m_Finished.notify_all();
}
void ProgressiveSession::start()
{
  if (!m_Backend)
    throw std::logic_error("Bind a progressive backend before starting its session.");
  stop();
  invalidate();
  const auto settings = m_Settings;
  if (!std::isfinite(settings.resolutionScale) || settings.resolutionScale <= 0 ||
      settings.resolutionScale > 8 || !std::isfinite(settings.exposureEV) ||
      std::abs(settings.exposureEV) > 64 || settings.minSamplesPerFrame == 0 || settings.minSamplesPerFrame > 1024)
    throw std::invalid_argument("Invalid renderer resolution scale or exposure.");
  m_Settings = settings;
  m_Backend->m_SessionActive = true;
  {
    std::lock_guard lock(m_Mutex);
    m_Stopped = false;
  }
  try {
    m_Thread = std::jthread([this](std::stop_token stop) { run(stop); });
  } catch (...) {
    m_Backend->m_SessionActive = false;
    std::lock_guard lock(m_Mutex);
    m_Stopped = true;
    throw;
  }
}
void ProgressiveSession::stop() noexcept
{
  {
    std::lock_guard lock(m_Mutex);
    m_Thread.request_stop();
    m_Cancel.request_stop();
    m_Pending.reset();
    m_Stopped = true;
  }
  m_Ready.notify_all();
  m_Finished.notify_all();
  if (m_Thread.joinable())
    m_Thread.join();
  if (m_Backend)
    m_Backend->m_SessionActive = false;
  {
    std::lock_guard lock(m_Mutex);
    m_Progress.inFlight = false;
  }
}
void ProgressiveSession::invalidate() noexcept
{
  std::lock_guard lock(m_Mutex);
  ++m_Epoch;
  m_Cancel.request_stop();
  m_Pending.reset();
  m_LastInput.reset();
  m_Image.reset();
  m_GpuFrame.reset();
  m_Progress = {};
  m_ResolvePending = false;
  m_Ready.notify_all();
  m_Finished.notify_all();
}
void ProgressiveSession::submit(Renderer::SceneSnapshot snapshot, Renderer::ImageSize framebuffer,
                                bool accumulate, std::chrono::steady_clock::time_point deadline)
{
  if (!snapshot.camera) {
    invalidate();
    return;
  }
  std::lock_guard lock(m_Mutex);
  const double width = framebuffer.width * static_cast<double>(m_Settings.resolutionScale);
  const double height = framebuffer.height * static_cast<double>(m_Settings.resolutionScale);
  if (width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max())
    throw std::invalid_argument("Scaled framebuffer exceeds supported dimensions.");
  const Renderer::ImageSize size{std::max(1, static_cast<int>(std::lround(width))),
                                 std::max(1, static_cast<int>(std::lround(height)))};
  (void)size.pixels();
  if (accumulate && m_LastInput && m_LastInput->accumulate &&
      m_LastInput->size == size && m_LastInput->snapshot == snapshot)
    return;
  if (m_LastInput && (m_LastInput->size != size || m_LastInput->accumulate != accumulate)) {
    ++m_Epoch;
    m_Cancel.request_stop();
    m_Image.reset();
    m_GpuFrame.reset();
    m_Progress = {};
  }
  auto input = std::make_shared<Input>(Input{std::move(snapshot), size, m_Epoch, ++m_Revision, accumulate, deadline});
  m_Pending = input;
  m_LastInput = std::move(input);
  m_Ready.notify_one();
  m_Finished.notify_all();
}
void ProgressiveSession::waitForFrame()
{
  std::unique_lock lock(m_Mutex);
  const auto input = m_LastInput;
  if (!input || input->accumulate) return;
  m_Finished.wait(lock, [&] {
    return m_Stopped || m_Error || input->epoch != m_Epoch || m_CompletedRevision >= input->revision;
  });
}
std::shared_ptr<const Renderer::Image> ProgressiveSession::image() const
{
  std::lock_guard lock(m_Mutex);
  return m_Image;
}
std::shared_ptr<GpuFrame> ProgressiveSession::gpuFrame() const {
  std::lock_guard lock(m_Mutex); return m_GpuFrame;
}
Renderer::RenderProgress ProgressiveSession::progress() const
{
  std::lock_guard lock(m_Mutex);
  return m_Progress;
}
void ProgressiveSession::rethrowError()
{
  std::exception_ptr error;
  {
    std::lock_guard lock(m_Mutex);
    error = std::exchange(m_Error, nullptr);
  }
  if (error)
    std::rethrow_exception(error);
}

void ProgressiveSession::run(std::stop_token stop)
{
  std::shared_ptr<const Input> current;
  std::unique_ptr<Renderer::Film> film;
  Renderer::RayScene scene;
  std::uint64_t samples = 0;
  std::chrono::steady_clock::duration sampleDuration{};
  try {
    for (;;) {
      bool changed = false;
      std::unique_lock lock(m_Mutex);
      m_Ready.wait(lock, [&] {
        if (stop.stop_requested() || m_Pending) return true;
        if (!current || current->epoch != m_Epoch) return false;
        if (m_ResolvePending) return true;
        const bool sampleAllowed = (m_Settings.maxSamples == 0 || samples < m_Settings.maxSamples) &&
          (current->accumulate || samples < m_Settings.minSamplesPerFrame ||
           std::chrono::steady_clock::now() + sampleDuration < current->deadline);
        if (!sampleAllowed && !current->accumulate && samples != 0) {
          m_CompletedRevision = current->revision;
          m_Finished.notify_all();
        }
        return sampleAllowed;
      });
      if (stop.stop_requested())
        return;
      if (current && current->epoch != m_Epoch) {
        current.reset();
        film.reset();
        samples = 0;
      }
      if (m_Pending) {
        current = std::exchange(m_Pending, nullptr);
        changed = true;
      }
      if (!current)
        continue;
      const auto settings = m_Settings;
      const auto settingsRevision = m_SettingsRevision;
      const bool resolveOnly = !changed && m_ResolvePending;
      m_ResolvePending = false;
      if (resolveOnly) {
        lock.unlock();
        auto image = m_Gpu ? nullptr : std::make_shared<Renderer::Image>(film->resolve(settings.exposureEV, m_Encoding));
        lock.lock();
        if (current->epoch == m_Epoch && settingsRevision == m_SettingsRevision && !stop.stop_requested()) {
          m_Image = std::move(image);
          m_Progress.revision = current->revision;
          m_Progress.size = current->size;
          m_Progress.rendererName = m_Backend->name();
          m_Progress.samplesPerPixel = samples;
          m_Progress.inFlight = false;
        }
        continue;
      }
      m_Cancel = std::stop_source();
      const auto token = m_Cancel.get_token();
      m_Progress.inFlight = true;
      lock.unlock();
      if (changed) {
        const auto prepare = [&] {
          scene = m_Compiler.compile(current->snapshot, token);
          film = m_Gpu ? nullptr : std::make_unique<Renderer::Film>(current->size);
          samples = 0;
        };
        if (m_Jobs) m_Jobs->wait(m_Jobs->submit(prepare, JobCategory::Rendering));
        else prepare();
      }
      if (token.stop_requested()) continue;
      const auto start = std::chrono::steady_clock::now();
      if (samples == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Sample count overflow.");
      // Independent running frames must not repeat the same jitter/path stream.
      // sampleIndex still starts at zero so CPU Film/GPU HDR reset per frame.
      const auto seed = current->accumulate ? settings.seed :
        settings.seed + current->revision * UINT64_C(0x9e3779b97f4a7c15);
      const Renderer::RenderRequest request{*current->snapshot.camera, current->size, samples, seed, token};
      std::shared_ptr<GpuFrame> gpuFrame;
      if (m_Gpu) gpuFrame = m_Backend->renderGpuFrame(scene, request);
      else m_Backend->render(scene, request, *film);
      if (token.stop_requested())
        continue;
      if ((m_Gpu && !gpuFrame) || (!m_Gpu && film->samples() != request.sampleIndex + 1))
        throw std::logic_error("Backend must commit exactly one sample round.");
      ++samples;
      auto image = m_Gpu ? nullptr : std::make_shared<Renderer::Image>(film->resolve(settings.exposureEV, m_Encoding));
      sampleDuration = std::chrono::steady_clock::now() - start;
      const double milliseconds = std::chrono::duration<double, std::milli>(sampleDuration).count();
      lock.lock();
      if (current->epoch == m_Epoch && (m_Gpu || settingsRevision == m_SettingsRevision) && !stop.stop_requested()) {
        m_Image = std::move(image);
        m_GpuFrame = std::move(gpuFrame);
        m_Progress = {};
        m_Progress.revision = current->revision;
        m_Progress.samplesPerPixel = samples;
        m_Progress.size = current->size;
        m_Progress.sampleMilliseconds = milliseconds;
        m_Progress.rendererName = m_Backend->name();
      }
    }
  } catch (...) {
    std::lock_guard lock(m_Mutex);
    m_Error = std::current_exception();
    m_Progress.inFlight = false;
    m_Finished.notify_all();
  }
}

} // namespace Jade::Detail
