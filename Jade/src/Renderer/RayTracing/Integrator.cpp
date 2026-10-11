#include "Integrator.hpp"
#include "../Common/Math.hpp"

#include <Jade/Renderer/RayTracing/BSDF.hpp>

namespace Jade::Renderer {

PathIntegrator::PathIntegrator(PathIntegratorConfig config) : m_Config(config)
{
  if (config.maxBounces == 0 || config.maxBounces > 1024 || config.rrStartBounce == 0)
    throw std::invalid_argument("Path depth must be within [1, 1024] and roulette start positive.");
}

LinearRGB PathIntegrator::Li(const Ray &initial, const RayScene &scene, Sampler &sampler) const
{
  LinearRGB radiance, throughput{1, 1, 1};
  Ray ray = initial;
  bool previousDelta = true;
  float previousPdf = 0, etaScale = 1;
  Vec3 previousPosition;
  for (std::uint32_t depth = 0;; ++depth) {
    SurfaceHit hit;
    if (!scene.intersect(ray, hit))
      break;
    const auto &material = scene.material(hit);
    if (material.twoSidedEmission || hit.frontFace) {
      const float weight =
          previousDelta ? 1 : Math::powerHeuristic(previousPdf, scene.emitterPdf(previousPosition, hit));
      radiance += throughput * material.emission * weight;
    }
    if (depth == m_Config.maxBounces)
      break;
    const BSDF bsdf(material, hit);
    const Vec3 outgoing = -ray.direction;
    const auto normal = material.model == MaterialModel::Dielectric ? hit.geometricNormal : hit.shadingNormal;
    if (!bsdf.delta() && scene.emitterCount() != 0) {
      const auto selection = sampler.next1D();
      const auto lightSample = sampler.next2D();
      const auto emitter = scene.sampleEmitter(selection, lightSample);
      const auto delta = emitter.position - hit.position;
      const float distance = Math::length(delta);
      if (distance > 0) {
        const auto direction = delta * (1 / distance);
        const float cosLight = dot(emitter.normal, -direction);
        if (emitter.areaPdf > 0 && (emitter.twoSided || cosLight > 0) && cosLight != 0) {
          const float lightPdf = emitter.areaPdf * distance * distance / std::abs(cosLight);
          const auto value = bsdf.evaluate(outgoing, direction);
          if (lightPdf > 0 && std::isfinite(lightPdf) && value.maximum() > 0) {
            auto shadow = hit.spawn(direction);
            SurfaceHit lightEndpoint;
            lightEndpoint.position = emitter.position;
            lightEndpoint.positionError = emitter.positionError;
            lightEndpoint.geometricNormal = emitter.normal;
            const auto target = lightEndpoint.spawn(-direction).origin - shadow.origin;
            const float length = Math::length(target);
            shadow.direction = Math::unit(target);
            shadow.tMax = std::nextafter(length, 0.0f);
            if (!scene.occluded(shadow)) {
              const float weight = Math::powerHeuristic(lightPdf, bsdf.pdf(outgoing, direction));
              radiance += throughput * value * emitter.emission *
                          (std::abs(dot(normal, direction)) * weight / lightPdf);
            }
          }
        }
      }
    }
    const auto sample = bsdf.sample(outgoing, sampler);
    if (!(sample.pdf > 0) || !sample.value.finite() || sample.value.maximum() == 0)
      break;
    throughput *= sample.value * (std::abs(dot(normal, sample.direction)) / sample.pdf);
    if (!throughput.finite())
      throw std::runtime_error("Path throughput exceeded floating-point range.");
    previousDelta = sample.delta;
    previousPdf = sample.pdf;
    previousPosition = hit.position;
    if (sample.transmission)
      etaScale *= sample.eta * sample.eta;
    ray = hit.spawn(sample.direction);
    if (depth + 1 >= m_Config.rrStartBounce) {
      const float survival = std::clamp(throughput.maximum() * etaScale, 0.05f, 0.95f);
      if (sampler.next1D() >= survival)
        break;
      throughput *= 1 / survival;
    }
  }
  return radiance;
}

} // namespace Jade::Renderer
