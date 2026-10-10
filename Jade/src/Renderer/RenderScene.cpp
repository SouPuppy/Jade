#include "RenderScene.hpp"
#include "Math.hpp"

#include <Jade/Graphics/Geometry.hpp>
#include <Jade/Renderer/BSDF.hpp>

#include "../internal/Core/SceneTransforms.hpp"
#include "../internal/Mesh/MeshAccess.hpp"
#include "../internal/Renderer/RenderSceneAccess.hpp"

#include <array>
#include <unordered_map>

namespace Jade::Renderer {
namespace {

using Jade::Detail::Bounds;
using Jade::Detail::BuildCancelled;
using Jade::Detail::BVH;
using Jade::Detail::CheckCancellation;
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
struct Emitter {
  Vec3 a, b, c, normal;
  LinearRGB emission;
  std::uint32_t instance, primitive;
  float area;
  double cumulative = 0, weight = 0;
  bool twoSided = false;
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

namespace {
struct EmitterDistribution {
  std::vector<Emitter> emitters;
  std::vector<std::unordered_map<std::uint32_t, std::size_t>> emitterIndices;
  double totalWeight = 0;
};
} // namespace

struct RenderScene::Impl {
  std::shared_ptr<const CompiledGeometry> geometry = std::make_shared<CompiledGeometry>();
  std::vector<Material> materials;
  std::shared_ptr<const EmitterDistribution> lighting = std::make_shared<EmitterDistribution>();
  Jade::Detail::SceneAccelerationUpdate update = Jade::Detail::SceneAccelerationUpdate::Rebuilt;
};
struct SceneCompiler::Impl {
  std::shared_ptr<const RenderScene::Impl> previous;
  SceneSnapshot input;
};

SceneSnapshot SceneSnapshot::capture(const Scene &scene, const Assets &assets)
{
  SceneSnapshot result;
  Jade::Detail::SceneTransforms transforms(scene);
  const auto camera = scene.camera();
  if (camera.has<Transform>() && camera.has<Camera>()) {
    const auto view = transforms.CameraView(camera);
    const auto inverse = Math::inverseAffine(view);
    CameraFrame frame;
    frame.lens = camera.get<Camera>();
    Jade::Detail::ValidateCamera(frame.lens);
    frame.position = Math::point(inverse, {});
    frame.right = Math::unit(Math::vector(inverse, {1, 0, 0}));
    frame.up = Math::unit(Math::vector(inverse, {0, 1, 0}));
    frame.forward = Math::unit(Math::vector(inverse, {0, 0, -1}));
    result.camera = frame;
  }
  for (const auto entity : scene.view<Geometry>()) {
    const auto handle = entity.get<Geometry>().mesh;
    if (handle.empty())
      continue;
    if (!entity.has<Material>())
      throw std::invalid_argument("Geometry requires a Material component.");
    const auto material = entity.get<Material>();
    validateMaterial(material);
    result.instances.push_back(
        {assets.share(handle), transforms.Resolve(entity).model, material, entity.id()});
  }
  return result;
}

RenderScene::RenderScene() : m_Impl(std::make_shared<Impl>()) {}
SceneCompiler::SceneCompiler() : m_Impl(std::make_unique<Impl>()) {}
SceneCompiler::~SceneCompiler() = default;
RenderScene RenderScene::build(const SceneSnapshot &snapshot) { return SceneCompiler().compile(snapshot); }

RenderScene SceneCompiler::compile(const SceneSnapshot &snapshot, std::stop_token cancellation)
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
  auto output = std::make_shared<RenderScene::Impl>();
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
    auto lighting = std::make_shared<EmitterDistribution>();
    lighting->emitterIndices.resize(snapshot.instances.size());
    for (std::size_t index = 0; index < snapshot.instances.size(); ++index) {
      CheckCancellation(cancellation);
      const auto &input = snapshot.instances[index];
      if (input.material.emission.maximum() == 0)
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
    output->lighting = std::move(lighting);
  }
  // Prepare all potentially throwing work before committing the compiler cache.
  auto input = snapshot;
  RenderScene result;
  result.m_Impl = output;
  CheckCancellation(cancellation);
  m_Impl->input = std::move(input);
  m_Impl->previous = std::move(output);
  return result;
} catch (const BuildCancelled &) {
  return {};
}

bool RenderScene::intersect(const Ray &ray, SurfaceHit &hit) const
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

bool RenderScene::occluded(const Ray &ray) const
{
  float maximum = ray.tMax;
  return m_Impl->geometry->bvh.visit(ray, maximum, true, [&](std::uint32_t index) {
    const auto &instance = m_Impl->geometry->instances[index];
    const Ray local{Math::point(instance.inverse, ray.origin), Math::vector(instance.inverse, ray.direction),
                    ray.tMin, ray.tMax};
    return MeshAccess::occluded(*instance.mesh, local);
  });
}

const Material &RenderScene::material(const SurfaceHit &hit) const
{
  return m_Impl->materials.at(hit.instance);
}
std::size_t RenderScene::instanceCount() const noexcept { return m_Impl->materials.size(); }
std::size_t RenderScene::emitterCount() const noexcept { return m_Impl->lighting->emitters.size(); }
std::size_t RenderScene::meshCount() const noexcept
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

EmitterSample RenderScene::sampleEmitter(float selection, Vec2 sample) const
{
  if (m_Impl->lighting->emitters.empty())
    return {};
  const double target = std::clamp(static_cast<double>(selection), 0.0, std::nextafter(1.0, 0.0)) *
                        m_Impl->lighting->totalWeight;
  const auto it =
      std::upper_bound(m_Impl->lighting->emitters.begin(), m_Impl->lighting->emitters.end(), target,
                       [](double value, const Emitter &emitter) { return value < emitter.cumulative; });
  const auto &emitter = it == m_Impl->lighting->emitters.end() ? m_Impl->lighting->emitters.back() : *it;
  const float root = std::sqrt(std::clamp(sample.x, 0.0f, 1.0f));
  const Vec3 weights{1 - root, root * (1 - sample.y), root * sample.y};
  const auto p = emitter.a * weights.x + emitter.b * weights.y + emitter.c * weights.z;
  const auto error = (Math::abs(emitter.a) * weights.x + Math::abs(emitter.b) * weights.y +
                      Math::abs(emitter.c) * weights.z) *
                     (16 * std::numeric_limits<float>::epsilon());
  return {p,
          emitter.normal,
          emitter.emission,
          static_cast<float>(emitter.weight / m_Impl->lighting->totalWeight / emitter.area),
          error,
          emitter.twoSided};
}

float RenderScene::emitterPdf(Vec3 reference, const SurfaceHit &hit) const
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
const void *RenderSceneAccess::geometryIdentity(const Renderer::RenderScene &scene)
{
  return scene.m_Impl->geometry.get();
}
const void *RenderSceneAccess::emitterIdentity(const Renderer::RenderScene &scene)
{
  return scene.m_Impl->lighting.get();
}
SceneAccelerationUpdate RenderSceneAccess::update(const Renderer::RenderScene &scene)
{
  return scene.m_Impl->update;
}
BVHStats RenderSceneAccess::statistics(const Renderer::RenderScene &scene)
{
  return scene.m_Impl->geometry->bvh.statistics();
}
} // namespace Jade::Detail
