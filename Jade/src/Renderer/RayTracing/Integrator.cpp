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
  return evaluate(initial,scene,sampler,nullptr);
}
LinearRGB PathIntegrator::record(const Ray& initial,const RayScene& scene,Sampler& sampler,std::vector<PathSegment>& segments) const {
  return evaluate(initial,scene,sampler,&segments);
}
LinearRGB PathIntegrator::evaluate(const Ray& initial,const RayScene& scene,Sampler& sampler,std::vector<PathSegment>* segments) const {
  LinearRGB radiance, throughput{1, 1, 1};
  Ray ray = initial;
  bool previousDelta = true;
  float previousPdf = 0, etaScale = 1;
  Vec3 previousPosition;
  for (std::uint32_t depth = 0;; ++depth) {
    SurfaceHit hit;
    if (!scene.intersect(ray, hit)) {
      if(segments) segments->push_back({ray.at(ray.tMin),ray.at(std::min(ray.tMax,30.f)),PathSegmentKind::Escape});
      break;
    }
    if(segments) segments->push_back({ray.at(ray.tMin),hit.position,depth==0?PathSegmentKind::Primary:PathSegmentKind::Bounce});
    if (scene.unlit(hit)) {
      radiance += throughput * scene.spriteSample(hit).color;
      break;
    }
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
            const bool blocked=scene.occluded(shadow);
            if(segments) {
              SurfaceHit blocker;const bool found=blocked && scene.intersect(shadow,blocker);
              segments->push_back({shadow.origin,found?blocker.position:shadow.at(shadow.tMax),blocked?PathSegmentKind::ShadowBlocked:PathSegmentKind::ShadowVisible});
            }
            if (!blocked) {
              const float weight = Math::powerHeuristic(lightPdf, bsdf.pdf(outgoing, direction));
              radiance += throughput * value * emitter.emission *
                          (std::abs(dot(normal, direction)) * weight / lightPdf);
            }
          }
        }
      }
    }
    if(!bsdf.delta()) for(const auto& light:scene.lights()) {
      Vec3 target=light.position;
      float area=1;
      if(light.type==LightType::Area) {
        const auto u=sampler.next2D();
        target=target+light.right*((u.x-.5f)*light.size.x)+light.up*((u.y-.5f)*light.size.y);
        area=light.size.x*light.size.y;
      }
      const auto delta=target-hit.position;
      const float distance=light.type==LightType::Directional?10000.f:Math::length(delta);
      if(distance<=0) continue;
      const auto direction=light.type==LightType::Directional?-light.direction:delta*(1/distance);
      auto irradiance=light.intensity;
      if(light.type!=LightType::Directional) irradiance=irradiance*(area/(distance*distance));
      if(light.type==LightType::Area) {
        const auto cosine=dot(light.direction,-direction);
        irradiance=irradiance*(light.twoSided?std::abs(cosine):std::max(0.f,cosine));
      }
      const auto value=bsdf.evaluate(outgoing,direction);
      if(value.maximum()<=0 || irradiance.maximum()<=0) continue;
      auto shadow=hit.spawn(direction);shadow.tMax=std::nextafter(distance,0.f);
      const bool blocked=scene.occluded(shadow);
      if(segments) {
        SurfaceHit blocker;const bool found=blocked && scene.intersect(shadow,blocker);
        segments->push_back({shadow.origin,found?blocker.position:shadow.at(std::min(shadow.tMax,30.f)),blocked?PathSegmentKind::ShadowBlocked:PathSegmentKind::ShadowVisible});
      }
      if(!blocked) radiance+=throughput*value*irradiance*std::abs(dot(normal,direction));
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
