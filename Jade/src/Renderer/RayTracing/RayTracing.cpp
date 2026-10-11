#include "RayTracing.hpp"
#include "Settings.hpp"

#include <Jade/Core/JobSystem.hpp>
#include <mutex>

namespace Jade::Renderer {
namespace {

void validateSettings(const RayTracingSettings &config)
{
  Detail::validateRayTracingSettings(config);
  if (config.device != RayDevice::CPU)
    throw std::invalid_argument("RayTracing is the CPU backend; use ComputeRayTracer for GPU execution.");
}

} // namespace

struct RayTracing::Impl {
  RayTracingSettings config;
  std::unique_ptr<JobSystem> ownedJobs;
  JobSystem* jobs;
  std::unique_ptr<Integrator> integrator;
  std::mutex renderMutex;
  Impl(RayTracingSettings input, JobSystem* external)
      : config(input), ownedJobs(external ? nullptr : std::make_unique<JobSystem>()),
        jobs(external ? external : ownedJobs.get()),
        integrator(std::make_unique<PathIntegrator>(PathIntegratorConfig{input.maxBounces, input.rrStartBounce})) {}
};

RayTracing::RayTracing(RayTracingSettings config) {
  validateSettings(config);
  m_Impl = std::make_unique<Impl>(config, nullptr);
}
RayTracing::RayTracing(JobSystem& jobs, RayTracingSettings config) {
  validateSettings(config);
  m_Impl = std::make_unique<Impl>(config, &jobs);
}
RayTracing::~RayTracing() = default;
ProgressiveSettings RayTracing::samplingSettings() const { return m_Impl->config; }
ImageEncoding RayTracing::imageEncoding() const noexcept {
  return m_Impl->config.output == RayOutput::Normal ? ImageEncoding::LinearData : ImageEncoding::Radiance;
}
void RayTracing::configure(const RayTracingSettings& settings)
{
  validateSettings(settings);
  std::lock_guard lock(m_Impl->renderMutex);
  if (settings.maxBounces != m_Impl->config.maxBounces || settings.rrStartBounce != m_Impl->config.rrStartBounce)
    m_Impl->integrator = std::make_unique<PathIntegrator>(PathIntegratorConfig{settings.maxBounces, settings.rrStartBounce});
  m_Impl->config = settings;
}
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
        const auto randomJitter = sampler.next2D();
        const auto jitter = m_Impl->config.jitter ? randomJitter : Vec2{0.5f, 0.5f};
        const auto ray = request.camera.generateRay({x + jitter.x, y + jitter.y}, size);
        if (m_Impl->config.output == RayOutput::Normal || m_Impl->config.mode == RayMode::RayCast) {
          SurfaceHit hit;
          if (scene.intersect(ray, hit)) {
            if (m_Impl->config.output == RayOutput::Normal) sample[pixel] = {
              hit.shadingNormal.x * 0.5f + 0.5f, hit.shadingNormal.y * 0.5f + 0.5f,
              hit.shadingNormal.z * 0.5f + 0.5f};
            else {
              const auto& material = scene.material(hit);
              sample[pixel] = scene.unlit(hit) ? scene.spriteSample(hit).color : material.baseColor;
              if (material.twoSidedEmission || hit.frontFace) sample[pixel] += material.emission;
            }
          }
        } else sample[pixel] = m_Impl->integrator->Li(ray, scene, sampler);
      }
    }
  }, {JobCategory::RayTracing, {}, 1});
  m_Impl->jobs->wait(round);
  if (!request.cancellation.stop_requested())
    film.addSample(sample);
}

} // namespace Jade::Renderer
