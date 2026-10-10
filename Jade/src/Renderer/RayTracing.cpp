#include "RayTracing.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace Jade::Renderer {
namespace {

// Persistent workers. The caller waits for a whole round; Application invokes
// it from its coordinator thread, never the window/event thread.
class ThreadPool final {
public:
  explicit ThreadPool(std::uint32_t count)
  {
    try {
      for (std::uint32_t i = 0; i < count; ++i)
        m_Workers.emplace_back([this](std::stop_token stop) { worker(stop); });
    } catch (...) {
      shutdown();
      throw;
    }
  }
  ~ThreadPool() { shutdown(); }
  void run(std::size_t count, std::function<void(std::size_t)> job)
  {
    std::unique_lock lock(m_Mutex);
    m_Count = count;
    m_Next = 0;
    m_Job = std::move(job);
    m_Error = nullptr;
    m_Remaining = m_Workers.size();
    ++m_Generation;
    m_Ready.notify_all();
    m_Done.wait(lock, [&] { return m_Remaining == 0; });
    m_Job = {};
    if (m_Error)
      std::rethrow_exception(m_Error);
  }

private:
  void shutdown()
  {
    {
      std::lock_guard lock(m_Mutex);
      for (auto &worker : m_Workers)
        worker.request_stop();
    }
    m_Ready.notify_all();
    for (auto &worker : m_Workers)
      if (worker.joinable())
        worker.join();
  }
  void worker(std::stop_token stop)
  {
    std::uint64_t generation = 0;
    for (;;) {
      std::unique_lock lock(m_Mutex);
      m_Ready.wait(lock, [&] { return stop.stop_requested() || m_Generation != generation; });
      if (stop.stop_requested())
        return;
      generation = m_Generation;
      lock.unlock();
      try {
        for (;;) {
          const auto index = m_Next.fetch_add(1);
          if (index >= m_Count)
            break;
          m_Job(index);
        }
      } catch (...) {
        lock.lock();
        if (!m_Error)
          m_Error = std::current_exception();
        lock.unlock();
      }
      lock.lock();
      if (--m_Remaining == 0)
        m_Done.notify_one();
    }
  }
  std::mutex m_Mutex;
  std::condition_variable m_Ready, m_Done;
  std::vector<std::jthread> m_Workers;
  std::function<void(std::size_t)> m_Job;
  std::atomic<std::size_t> m_Next = 0;
  std::size_t m_Count = 0, m_Remaining = 0;
  std::uint64_t m_Generation = 0;
  std::exception_ptr m_Error;
};

std::uint32_t workerCount(std::uint32_t requested)
{
  if (requested > 256)
    throw std::invalid_argument("Worker count must be within [0, 256].");
  const auto hardware = std::thread::hardware_concurrency();
  return requested ? requested : std::clamp(hardware > 1 ? hardware - 1 : 1u, 1u, 256u);
}
void validateSettings(const RayTracingConfig &config)
{
  if (!std::isfinite(config.resolutionScale) || config.resolutionScale <= 0 || config.resolutionScale > 8)
    throw std::invalid_argument("Resolution scale must be within (0, 8].");
  if (!std::isfinite(config.exposureEV) || std::abs(config.exposureEV) > 64)
    throw std::invalid_argument("Exposure must be within [-64, 64] EV.");
}

} // namespace

struct RayTracing::Impl {
  RayTracingConfig config;
  std::unique_ptr<Integrator> integrator;
  ThreadPool pool;
  std::mutex renderMutex;
  explicit Impl(RayTracingConfig input)
      : config(input), integrator(std::make_unique<PathIntegrator>(
                           PathIntegratorConfig{input.maxBounces, input.rrStartBounce})),
        pool(workerCount(input.workerCount))
  {
  }
};

RayTracing::RayTracing(RayTracingConfig config)
{
  validateSettings(config);
  m_Impl = std::make_unique<Impl>(config);
}
RayTracing::~RayTracing() = default;
RenderSettings RayTracing::settings() const { return m_Impl->config; }
void RayTracing::setIntegrator(std::unique_ptr<Integrator> integrator)
{
  assertConfigurable();
  if (!integrator)
    throw std::invalid_argument("Integrator cannot be null.");
  std::unique_lock lock(m_Impl->renderMutex, std::try_to_lock);
  if (!lock.owns_lock())
    throw std::logic_error("Cannot change an integrator during a render round.");
  m_Impl->integrator = std::move(integrator);
}

void RayTracing::render(const RenderScene &scene, const RenderRequest &request, Film &film)
{
  std::unique_lock lock(m_Impl->renderMutex, std::try_to_lock);
  if (!lock.owns_lock())
    throw std::logic_error("Concurrent rendering on the same backend is unsupported.");
  if (request.size != film.size() || request.sampleIndex != film.samples())
    throw std::invalid_argument("RenderRequest size/sample index must match Film.");
  if (request.cancellation.stop_requested())
    return;
  (void)request.camera.generateRay({0.5f, 0.5f}, request.size);
  const auto size = request.size;
  std::vector<LinearRGB> sample(size.pixels());
  const auto columns = (static_cast<std::size_t>(size.width) + 15) / 16;
  const auto rows = (static_cast<std::size_t>(size.height) + 15) / 16;
  m_Impl->pool.run(columns * rows, [&](std::size_t tile) {
    if (request.cancellation.stop_requested())
      return;
    IndependentSampler sampler(request.seed);
    const int left = static_cast<int>(tile % columns) * 16, top = static_cast<int>(tile / columns) * 16;
    for (int y = top; y < std::min(top + 16, size.height); ++y) {
      if (request.cancellation.stop_requested())
        return;
      for (int x = left; x < std::min(left + 16, size.width); ++x) {
        const auto pixel = static_cast<std::size_t>(y) * size.width + x;
        sampler.start(pixel, request.sampleIndex);
        const auto jitter = sampler.next2D();
        const auto ray = request.camera.generateRay({x + jitter.x, y + jitter.y}, size);
        sample[pixel] = m_Impl->integrator->Li(ray, scene, sampler);
      }
    }
  });
  if (!request.cancellation.stop_requested())
    film.addSample(sample);
}

} // namespace Jade::Renderer
