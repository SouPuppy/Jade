#include "RayTracing.hpp"

#include <Jade/Core/JobSystem.hpp>
#include <mutex>

namespace Jade::Renderer {
namespace {

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
  std::unique_ptr<JobSystem> ownedJobs;
  JobSystem* jobs;
  std::unique_ptr<Integrator> integrator;
  std::mutex renderMutex;
  Impl(RayTracingConfig input, JobSystem* external)
      : config(input), ownedJobs(external ? nullptr : std::make_unique<JobSystem>()),
        jobs(external ? external : ownedJobs.get()),
        integrator(std::make_unique<PathIntegrator>(PathIntegratorConfig{input.maxBounces, input.rrStartBounce})) {}
};

RayTracing::RayTracing(RayTracingConfig config) {
  validateSettings(config);
  m_Impl = std::make_unique<Impl>(config, nullptr);
}
RayTracing::RayTracing(JobSystem& jobs, RayTracingConfig config) {
  validateSettings(config);
  m_Impl = std::make_unique<Impl>(config, &jobs);
}
RayTracing::~RayTracing() = default;
ProgressiveSettings RayTracing::samplingSettings() const { return m_Impl->config; }
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

void RayTracing::render(const RayScene &scene, const RenderRequest &request, Film &film)
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
  const auto round = m_Impl->jobs->parallelFor(columns * rows, [&](std::size_t tile) {
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
  }, {JobCategory::RayTracing, {}, 1});
  m_Impl->jobs->wait(round);
  if (!request.cancellation.stop_requested())
    film.addSample(sample);
}

} // namespace Jade::Renderer
