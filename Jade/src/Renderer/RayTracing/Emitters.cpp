#include "Emitters.hpp"
#include "../Common/Math.hpp"
#include "../../internal/Acceleration/BVH.hpp"
namespace Jade::Detail {
namespace Math = Renderer::Math;
std::shared_ptr<const EmitterDistribution> compileEmitters(const Renderer::SceneSnapshot &snapshot,
                                                           std::stop_token cancellation)
{
  auto lighting = std::make_shared<EmitterDistribution>();
  lighting->emitterIndices.resize(snapshot.instances.size());
  for (std::size_t index = 0; index < snapshot.instances.size(); ++index) {
    CheckCancellation(cancellation);
    const auto &input = snapshot.instances[index];
    if (input.unlit || input.material.emission.maximum() == 0)
      continue;
    const auto &mesh = input.mesh->data();
    for (std::size_t p = 0; p < mesh.indices.size() / 3; ++p) {
      if ((p & 1023) == 0)
        CheckCancellation(cancellation);
      const auto a = Math::point(input.objectToWorld, mesh.vertices[mesh.indices[p * 3]].position);
      const auto b = Math::point(input.objectToWorld, mesh.vertices[mesh.indices[p * 3 + 1]].position);
      const auto c = Math::point(input.objectToWorld, mesh.vertices[mesh.indices[p * 3 + 2]].position);
      const auto crossProduct = cross(b - a, c - a);
      const float area = Math::length(crossProduct) / 2;
      if (!std::isfinite(area))
        throw std::invalid_argument("Emitter area exceeds floating-point range.");
      if (area == 0)
        continue;
      const double weight = static_cast<double>(area) * input.material.emission.luminance();
      lighting->totalWeight += weight;
      lighting->emitterIndices[index].emplace(static_cast<std::uint32_t>(p), lighting->emitters.size());
      lighting->emitters.push_back({a, b, c, Math::unit(crossProduct), input.material.emission,
                                    static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(p), area,
                                    lighting->totalWeight, weight, input.material.twoSidedEmission});
    }
  }
  return lighting;
}
Renderer::EmitterSample EmitterDistribution::sample(float selection, Vec2 sample) const
{
  if (emitters.empty())
    return {};
  const double target =
      std::clamp(static_cast<double>(selection), 0.0, std::nextafter(1.0, 0.0)) * totalWeight;
  const auto it =
      std::upper_bound(emitters.begin(), emitters.end(), target,
                       [](double value, const Emitter &emitter) { return value < emitter.cumulative; });
  const auto &emitter = it == emitters.end() ? emitters.back() : *it;
  const float root = std::sqrt(std::clamp(sample.x, 0.0f, 1.0f));
  const Vec3 weights{1 - root, root * (1 - sample.y), root * sample.y};
  const auto p = emitter.a * weights.x + emitter.b * weights.y + emitter.c * weights.z;
  const auto error = (Math::abs(emitter.a) * weights.x + Math::abs(emitter.b) * weights.y +
                      Math::abs(emitter.c) * weights.z) *
                     (16 * std::numeric_limits<float>::epsilon());
  return {p,
          emitter.normal,
          emitter.emission,
          static_cast<float>(emitter.weight / totalWeight / emitter.area),
          error,
          emitter.twoSided};
}

bool sameEmitters(const Renderer::SceneSnapshot &a, const Renderer::SceneSnapshot &b)
{
  if (a.instances.size() != b.instances.size())
    return false;
  for (std::size_t i = 0; i < a.instances.size(); ++i) {
    const auto &x = a.instances[i];
    const auto &y = b.instances[i];
    if (x.mesh != y.mesh || x.entity != y.entity || x.unlit != y.unlit || x.material.emission != y.material.emission ||
        x.material.twoSidedEmission != y.material.twoSidedEmission ||
        ((x.material.emission.maximum() > 0 || y.material.emission.maximum() > 0) &&
         x.objectToWorld.values != y.objectToWorld.values))
      return false;
  }
  return true;
}
} // namespace Jade::Detail
