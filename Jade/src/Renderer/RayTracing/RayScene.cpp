#include "RayScene.hpp"
#include "../Common/Math.hpp"
#include "Emitters.hpp"

#include <Jade/Graphics/Geometry.hpp>
#include <Jade/Renderer/RayTracing/BSDF.hpp>

#include "../../internal/Core/SceneTransforms.hpp"
#include "../../internal/Mesh/MeshAccess.hpp"
#include "../../internal/Renderer/RaySceneAccess.hpp"

#include <array>
#include <unordered_map>

namespace Jade::Renderer {
namespace {

using Jade::Detail::Bounds;
using Jade::Detail::BuildCancelled;
using Jade::Detail::BVH;
using Jade::Detail::CheckCancellation;
using Jade::Detail::EmitterDistribution;
using Jade::Detail::MeshAccess;

struct InstanceGeometry {
  std::shared_ptr<const Mesh> mesh;
  Matrix4 model, inverse;
  std::uint64_t entity;
};
struct CompiledGeometry {
  std::vector<InstanceGeometry> instances;
  BVH bvh;
};
Vec3 transformError(const Matrix4 &model, Vec3 position, Vec3 error)
{
  const float gamma = 8 * std::numeric_limits<float>::epsilon();
  Vec3 result;
  float *output[3] = {&result.x, &result.y, &result.z};
  for (std::size_t row = 0; row < 3; ++row)
    *output[row] =
        std::abs(model.at(row, 0)) * error.x + std::abs(model.at(row, 1)) * error.y +
        std::abs(model.at(row, 2)) * error.z +
        gamma * (std::abs(model.at(row, 0) * position.x) + std::abs(model.at(row, 1) * position.y) +
                 std::abs(model.at(row, 2) * position.z) + std::abs(model.at(row, 3)));
  return result;
}

} // namespace

struct RayScene::Impl {
  std::shared_ptr<const CompiledGeometry> geometry = std::make_shared<CompiledGeometry>();
  std::vector<Material> materials;
  std::shared_ptr<const EmitterDistribution> lighting = std::make_shared<EmitterDistribution>();
  Jade::Detail::SceneAccelerationUpdate update = Jade::Detail::SceneAccelerationUpdate::Rebuilt;
};
struct RaySceneCompiler::Impl {
  std::shared_ptr<const RayScene::Impl> previous;
  SceneSnapshot input;
};

RayScene::RayScene() : m_Impl(std::make_shared<Impl>()) {}
RaySceneCompiler::RaySceneCompiler() : m_Impl(std::make_unique<Impl>()) {}
RaySceneCompiler::~RaySceneCompiler() = default;
RayScene RayScene::build(const SceneSnapshot &snapshot) { return RaySceneCompiler().compile(snapshot); }

RayScene RaySceneCompiler::compile(const SceneSnapshot &snapshot, std::stop_token cancellation)
try {
  CheckCancellation(cancellation);
  if (snapshot.instances.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument("Too many render instances.");
  for (const auto &instance : snapshot.instances) {
    CheckCancellation(cancellation);
    if (!instance.mesh)
      throw std::invalid_argument("Render instance has no compiled mesh resource.");
    validateMaterial(instance.material);
  }
  bool sameLayout = m_Impl->previous && m_Impl->input.instances.size() == snapshot.instances.size();
  bool sameTransforms = sameLayout;
  bool sameEmitters = sameLayout;
  if (sameLayout) {
    for (std::size_t i = 0; i < snapshot.instances.size(); ++i) {
      CheckCancellation(cancellation);
      const auto &before = m_Impl->input.instances[i];
      const auto &after = snapshot.instances[i];
      if (before.mesh != after.mesh || before.entity != after.entity) {
        sameLayout = sameTransforms = sameEmitters = false;
        break;
      }
      const bool moved = before.objectToWorld.values != after.objectToWorld.values;
      if (moved)
        sameTransforms = false;
      if (before.material.emission != after.material.emission ||
          before.material.twoSidedEmission != after.material.twoSidedEmission ||
          (moved && (before.material.emission.maximum() > 0 || after.material.emission.maximum() > 0)))
        sameEmitters = false;
    }
  }
  auto output = std::make_shared<RayScene::Impl>();
  if (sameTransforms) {
    output->geometry = m_Impl->previous->geometry;
    output->update = Jade::Detail::SceneAccelerationUpdate::Reused;
  } else {
    auto geometry = std::make_shared<CompiledGeometry>();
    std::vector<Bounds> bounds;
    bounds.reserve(snapshot.instances.size());
    geometry->instances.reserve(snapshot.instances.size());
    for (const auto &instance : snapshot.instances) {
      CheckCancellation(cancellation);
      const auto inverse = Math::inverseAffine(instance.objectToWorld);
      geometry->instances.push_back({instance.mesh, instance.objectToWorld, inverse, instance.entity});
      Bounds transformed;
      const auto local = MeshAccess::acceleration(*instance.mesh).bounds();
      if (local.valid())
        for (int corner = 0; corner < 8; ++corner) {
          const auto p = Math::point(instance.objectToWorld, {corner & 1 ? local.high.x : local.low.x,
                                                              corner & 2 ? local.high.y : local.low.y,
                                                              corner & 4 ? local.high.z : local.low.z});
          Jade::Detail::Validate(p);
          transformed.add(p);
        }
      bounds.push_back(transformed);
    }
    if (sameLayout) {
      geometry->bvh = m_Impl->previous->geometry->bvh;
      geometry->bvh.refit(bounds, cancellation);
      output->update = Jade::Detail::SceneAccelerationUpdate::Refit;
      if (geometry->bvh.statistics().cost > 1.5 * geometry->bvh.buildCost()) {
        geometry->bvh.build(bounds, cancellation);
        output->update = Jade::Detail::SceneAccelerationUpdate::Rebuilt;
      }
    } else
      geometry->bvh.build(bounds, cancellation);
    output->geometry = std::move(geometry);
  }
  for (const auto &instance : snapshot.instances)
    output->materials.push_back(instance.material);
  if (sameEmitters)
    output->lighting = m_Impl->previous->lighting;
  else {
    output->lighting = Jade::Detail::compileEmitters(snapshot, cancellation);
  }
  // Prepare all potentially throwing work before committing the compiler cache.
  auto input = snapshot;
  RayScene result;
  result.m_Impl = output;
  CheckCancellation(cancellation);
  m_Impl->input = std::move(input);
  m_Impl->previous = std::move(output);
  return result;
} catch (const BuildCancelled &) {
  return {};
}

bool RayScene::intersect(const Ray &ray, SurfaceHit &hit) const
{
  float maximum = ray.tMax;
  return m_Impl->geometry->bvh.visit(ray, maximum, false, [&](std::uint32_t index) {
    const auto &instance = m_Impl->geometry->instances[index];
    const Ray local{Math::point(instance.inverse, ray.origin), Math::vector(instance.inverse, ray.direction),
                    ray.tMin, maximum};
    SurfaceHit candidate;
    if (!MeshAccess::intersect(*instance.mesh, local, candidate))
      return false;
    maximum = candidate.t;
    const auto position = candidate.position;
    candidate.positionError = transformError(instance.model, position, candidate.positionError);
    candidate.position = Math::point(instance.model, position);
    candidate.geometricNormal = Math::normal(instance.inverse, candidate.geometricNormal);
    candidate.shadingNormal = Math::normal(instance.inverse, candidate.shadingNormal);
    candidate.frontFace = dot(ray.direction, candidate.geometricNormal) < 0;
    candidate.instance = index;
    candidate.entity = instance.entity;
    hit = candidate;
    return true;
  });
}

bool RayScene::occluded(const Ray &ray) const
{
  float maximum = ray.tMax;
  return m_Impl->geometry->bvh.visit(ray, maximum, true, [&](std::uint32_t index) {
    const auto &instance = m_Impl->geometry->instances[index];
    const Ray local{Math::point(instance.inverse, ray.origin), Math::vector(instance.inverse, ray.direction),
                    ray.tMin, ray.tMax};
    return MeshAccess::occluded(*instance.mesh, local);
  });
}

const Material &RayScene::material(const SurfaceHit &hit) const { return m_Impl->materials.at(hit.instance); }
std::size_t RayScene::instanceCount() const noexcept { return m_Impl->materials.size(); }
std::size_t RayScene::emitterCount() const noexcept { return m_Impl->lighting->emitters.size(); }
std::size_t RayScene::meshCount() const noexcept
{
  std::size_t count = 0;
  const auto &instances = m_Impl->geometry->instances;
  for (std::size_t i = 0; i < instances.size(); ++i) {
    bool first = true;
    for (std::size_t j = 0; j < i; ++j)
      if (instances[j].mesh == instances[i].mesh) {
        first = false;
        break;
      }
    if (first)
      ++count;
  }
  return count;
}

EmitterSample RayScene::sampleEmitter(float selection, Vec2 sample) const
{
  return m_Impl->lighting->sample(selection, sample);
}

float RayScene::emitterPdf(Vec3 reference, const SurfaceHit &hit) const
{
  if (hit.instance >= m_Impl->lighting->emitterIndices.size())
    return 0;
  const auto &indices = m_Impl->lighting->emitterIndices[hit.instance];
  const auto it = indices.find(hit.primitive);
  if (it == indices.end())
    return 0;
  const auto &emitter = m_Impl->lighting->emitters[it->second];
  const auto delta = hit.position - reference;
  const float distance = Math::length(delta);
  if (distance == 0)
    return 0;
  const float cosine = dot(emitter.normal, delta * (-1 / distance));
  if (!emitter.twoSided && cosine <= 0)
    return 0;
  const float absolute = std::abs(cosine);
  return absolute > 0 ? static_cast<float>(emitter.weight / m_Impl->lighting->totalWeight / emitter.area) *
                            distance * distance / absolute
                      : 0;
}

} // namespace Jade::Renderer

namespace Jade::Detail {
const void *RaySceneAccess::geometryIdentity(const Renderer::RayScene &scene)
{
  return scene.m_Impl->geometry.get();
}
const void *RaySceneAccess::emitterIdentity(const Renderer::RayScene &scene)
{
  return scene.m_Impl->lighting.get();
}
SceneAccelerationUpdate RaySceneAccess::update(const Renderer::RayScene &scene)
{
  return scene.m_Impl->update;
}
BVHStats RaySceneAccess::statistics(const Renderer::RayScene &scene)
{
  return scene.m_Impl->geometry->bvh.statistics();
}
} // namespace Jade::Detail
