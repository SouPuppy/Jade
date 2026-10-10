#include "RenderScene.hpp"
#include "Math.hpp"

#include <Jade/Graphics/Geometry.hpp>
#include <Jade/Renderer/BSDF.hpp>

#include "../internal/Core/SceneTransforms.hpp"
#include "../internal/Mesh/MeshValidation.hpp"

#include <array>
#include <numeric>
#include <unordered_map>

namespace Jade::Renderer {
namespace {

struct BuildCancelled {};
void checkCancellation(std::stop_token cancellation)
{
  if (cancellation.stop_requested())
    throw BuildCancelled{};
}

float coordinate(Vec3 v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

struct Bounds {
  Vec3 low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
  bool valid() const { return low.x <= high.x && low.y <= high.y && low.z <= high.z; }
  void add(Vec3 p)
  {
    low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
    high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
  }
  void add(const Bounds &other)
  {
    if (other.valid()) {
      add(other.low);
      add(other.high);
    }
  }
  Vec3 center() const { return low * 0.5f + high * 0.5f; }
  bool hit(const Ray &ray, float maximum) const
  {
    double near = ray.tMin, far = std::min(ray.tMax, maximum);
    for (int axis = 0; axis < 3; ++axis) {
      const double origin = coordinate(ray.origin, axis), direction = coordinate(ray.direction, axis);
      const double lo = std::nextafter(coordinate(low, axis), -INFINITY);
      const double hi = std::nextafter(coordinate(high, axis), INFINITY);
      if (direction == 0) {
        if (origin < lo || origin > hi)
          return false;
        continue;
      }
      double a = (lo - origin) / direction, b = (hi - origin) / direction;
      if (a > b)
        std::swap(a, b);
      near = std::max(near, a);
      far = std::min(far, b);
      if (near > far)
        return false;
    }
    return true;
  }
};

class BVH {
  struct Node {
    Bounds bounds;
    std::uint32_t start = 0, count = 0, left = 0, right = 0;
  };
  std::vector<Node> m_Nodes;
  std::vector<std::uint32_t> m_Order;
  std::uint32_t buildNode(std::size_t first, std::size_t count, const std::vector<Bounds> &bounds,
                          std::stop_token cancellation)
  {
    checkCancellation(cancellation);
    if (m_Nodes.size() >= std::numeric_limits<std::uint32_t>::max())
      throw std::overflow_error("BVH node count overflow.");
    const auto index = static_cast<std::uint32_t>(m_Nodes.size());
    m_Nodes.emplace_back();
    Bounds box, centroids;
    for (std::size_t i = first; i < first + count; ++i) {
      if ((i & 1023) == 0)
        checkCancellation(cancellation);
      box.add(bounds[m_Order[i]]);
      centroids.add(bounds[m_Order[i]].center());
    }
    m_Nodes[index].bounds = box;
    if (count <= 4) {
      m_Nodes[index].start = static_cast<std::uint32_t>(first);
      m_Nodes[index].count = static_cast<std::uint32_t>(count);
    } else {
      const auto extent = centroids.high - centroids.low;
      const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : extent.y >= extent.z ? 1 : 2;
      const auto middle = first + count / 2;
      std::nth_element(m_Order.begin() + static_cast<std::ptrdiff_t>(first),
                       m_Order.begin() + static_cast<std::ptrdiff_t>(middle),
                       m_Order.begin() + static_cast<std::ptrdiff_t>(first + count), [&](auto a, auto b) {
                         const auto ca = coordinate(bounds[a].center(), axis),
                                    cb = coordinate(bounds[b].center(), axis);
                         return ca == cb ? a < b : ca < cb;
                       });
      const auto left = buildNode(first, count / 2, bounds, cancellation);
      const auto right = buildNode(middle, count - count / 2, bounds, cancellation);
      m_Nodes[index].left = left;
      m_Nodes[index].right = right;
    }
    return index;
  }

public:
  Bounds bounds() const { return m_Nodes.empty() ? Bounds{} : m_Nodes.front().bounds; }
  void build(const std::vector<Bounds> &bounds, std::stop_token cancellation)
  {
    for (std::size_t i = 0; i < bounds.size(); ++i) {
      if ((i & 1023) == 0)
        checkCancellation(cancellation);
      if (bounds[i].valid())
        m_Order.push_back(static_cast<std::uint32_t>(i));
    }
    if (!m_Order.empty())
      buildNode(0, m_Order.size(), bounds, cancellation);
  }
  template <class Callback> bool visit(const Ray &ray, float &maximum, bool any, Callback &&callback) const
  {
    if (m_Nodes.empty())
      return false;
    std::array<std::uint32_t, 64> stack{};
    std::size_t pending = 1;
    bool found = false;
    while (pending) {
      const auto &node = m_Nodes[stack[--pending]];
      if (!node.bounds.hit(ray, maximum))
        continue;
      if (node.count) {
        for (std::uint32_t j = 0; j < node.count; ++j) {
          if (callback(m_Order[node.start + j])) {
            found = true;
            if (any)
              return true;
          }
        }
      } else {
        if (pending + 2 > stack.size())
          throw std::overflow_error("BVH traversal stack overflow.");
        stack[pending++] = node.right;
        stack[pending++] = node.left;
      }
    }
    return found;
  }
};

struct MeshBVH {
  std::shared_ptr<const Mesh> mesh;
  BVH bvh;
  explicit MeshBVH(std::shared_ptr<const Mesh> source, std::stop_token cancellation) : mesh(std::move(source))
  {
    Jade::Detail::ValidateMesh(*mesh);
    std::vector<Bounds> bounds(mesh->indices.size() / 3);
    for (std::size_t i = 0; i < bounds.size(); ++i) {
      if ((i & 1023) == 0)
        checkCancellation(cancellation);
      const auto a = mesh->vertices[mesh->indices[i * 3]].position;
      const auto b = mesh->vertices[mesh->indices[i * 3 + 1]].position;
      const auto c = mesh->vertices[mesh->indices[i * 3 + 2]].position;
      if (Math::length(cross(b - a, c - a)) > 0) {
        bounds[i].add(a);
        bounds[i].add(b);
        bounds[i].add(c);
      }
    }
    bvh.build(bounds, cancellation);
  }
};

// Sheared ray-space triangle test. Shared edges use the same calculations.
bool triangle(const Mesh &mesh, std::uint32_t primitive, const Ray &ray, float maximum, SurfaceHit &hit)
{
  const auto &a = mesh.vertices[mesh.indices[static_cast<std::size_t>(primitive) * 3]];
  const auto &b = mesh.vertices[mesh.indices[static_cast<std::size_t>(primitive) * 3 + 1]];
  const auto &c = mesh.vertices[mesh.indices[static_cast<std::size_t>(primitive) * 3 + 2]];
  int kz = 0;
  if (std::abs(ray.direction.y) > std::abs(coordinate(ray.direction, kz)))
    kz = 1;
  if (std::abs(ray.direction.z) > std::abs(coordinate(ray.direction, kz)))
    kz = 2;
  if (coordinate(ray.direction, kz) == 0)
    return false;
  int kx = (kz + 1) % 3, ky = (kx + 1) % 3;
  if (coordinate(ray.direction, kz) < 0)
    std::swap(kx, ky);
  const double sx = static_cast<double>(coordinate(ray.direction, kx)) / coordinate(ray.direction, kz);
  const double sy = static_cast<double>(coordinate(ray.direction, ky)) / coordinate(ray.direction, kz);
  const double sz = 1.0 / coordinate(ray.direction, kz);
  const Vec3 vertices[3] = {a.position, b.position, c.position};
  double x[3], y[3], z[3];
  for (int i = 0; i < 3; ++i) {
    const double px = static_cast<double>(coordinate(vertices[i], kx)) - coordinate(ray.origin, kx);
    const double py = static_cast<double>(coordinate(vertices[i], ky)) - coordinate(ray.origin, ky);
    const double pz = static_cast<double>(coordinate(vertices[i], kz)) - coordinate(ray.origin, kz);
    x[i] = px - sx * pz;
    y[i] = py - sy * pz;
    z[i] = pz * sz;
  }
  const double e[3] = {x[1] * y[2] - y[1] * x[2], x[2] * y[0] - y[2] * x[0], x[0] * y[1] - y[0] * x[1]};
  if ((e[0] < 0 || e[1] < 0 || e[2] < 0) && (e[0] > 0 || e[1] > 0 || e[2] > 0))
    return false;
  const double determinant = e[0] + e[1] + e[2];
  if (determinant == 0)
    return false;
  const double t = (e[0] * z[0] + e[1] * z[1] + e[2] * z[2]) / determinant;
  if (t <= ray.tMin || t > std::min(ray.tMax, maximum) || !std::isfinite(t))
    return false;
  const Vec3 weights{static_cast<float>(e[0] / determinant), static_cast<float>(e[1] / determinant),
                     static_cast<float>(e[2] / determinant)};
  const auto geometric = cross(b.position - a.position, c.position - a.position);
  if (Math::length(geometric) == 0)
    return false;
  hit.t = static_cast<float>(t);
  hit.barycentric = weights;
  hit.primitive = primitive;
  hit.position = a.position * weights.x + b.position * weights.y + c.position * weights.z;
  hit.geometricNormal = Math::unit(geometric);
  const auto shading = a.normal * weights.x + b.normal * weights.y + c.normal * weights.z;
  hit.shadingNormal = Math::length(shading) > 0 ? Math::unit(shading) : hit.geometricNormal;
  if (dot(hit.shadingNormal, hit.geometricNormal) < 0)
    hit.shadingNormal = -hit.shadingNormal;
  constexpr float error = 8 * std::numeric_limits<float>::epsilon();
  hit.positionError = (Math::abs(a.position) * weights.x + Math::abs(b.position) * weights.y +
                       Math::abs(c.position) * weights.z) *
                      error;
  return true;
}

struct InstanceGeometry {
  std::shared_ptr<const MeshBVH> mesh;
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

struct RenderScene::Impl {
  std::shared_ptr<const CompiledGeometry> geometry = std::make_shared<CompiledGeometry>();
  std::vector<Material> materials;
  std::vector<Emitter> emitters;
  std::vector<std::unordered_map<std::uint32_t, std::size_t>> emitterIndices;
  double totalWeight = 0;
};
struct SceneCompiler::Impl {
  std::unordered_map<const Mesh *, std::shared_ptr<const MeshBVH>> meshes;
  std::shared_ptr<const CompiledGeometry> previous;
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
  checkCancellation(cancellation);
  if (snapshot.instances.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument("Too many render instances.");
  bool unchanged = m_Impl->previous && m_Impl->previous->instances.size() == snapshot.instances.size();
  if (unchanged)
    for (std::size_t i = 0; i < snapshot.instances.size(); ++i) {
      const auto &previous = m_Impl->previous->instances[i];
      const auto &input = snapshot.instances[i];
      if (previous.mesh->mesh != input.mesh || previous.model.values != input.objectToWorld.values ||
          previous.entity != input.entity) {
        unchanged = false;
        break;
      }
    }
  auto output = std::make_shared<RenderScene::Impl>();
  if (unchanged)
    output->geometry = m_Impl->previous;
  else {
    auto geometry = std::make_shared<CompiledGeometry>();
    std::vector<Bounds> bounds;
    for (const auto &instance : snapshot.instances) {
      checkCancellation(cancellation);
      if (!instance.mesh)
        throw std::invalid_argument("Render instance has no mesh resource.");
      auto &mesh = m_Impl->meshes[instance.mesh.get()];
      if (!mesh)
        mesh = std::make_shared<MeshBVH>(instance.mesh, cancellation);
      const auto inverse = Math::inverseAffine(instance.objectToWorld);
      geometry->instances.push_back({mesh, instance.objectToWorld, inverse, instance.entity});
      Bounds transformed;
      const auto local = mesh->bvh.bounds();
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
    geometry->bvh.build(bounds, cancellation);
    output->geometry = geometry;
    m_Impl->previous = geometry;
  }
  output->emitterIndices.resize(snapshot.instances.size());
  for (std::size_t index = 0; index < snapshot.instances.size(); ++index) {
    checkCancellation(cancellation);
    const auto &input = snapshot.instances[index];
    validateMaterial(input.material);
    output->materials.push_back(input.material);
    if (input.material.emission.maximum() == 0)
      continue;
    const auto &mesh = *input.mesh;
    for (std::size_t p = 0; p < mesh.indices.size() / 3; ++p) {
      if ((p & 1023) == 0)
        checkCancellation(cancellation);
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
      output->totalWeight += weight;
      output->emitterIndices[index].emplace(static_cast<std::uint32_t>(p), output->emitters.size());
      output->emitters.push_back({a, b, c, Math::unit(crossProduct), input.material.emission,
                                  static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(p), area,
                                  output->totalWeight, weight, input.material.twoSidedEmission});
    }
  }
  RenderScene result;
  result.m_Impl = std::move(output);
  checkCancellation(cancellation);
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
    return instance.mesh->bvh.visit(local, maximum, false, [&](std::uint32_t primitive) {
      SurfaceHit candidate;
      if (!triangle(*instance.mesh->mesh, primitive, local, maximum, candidate))
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
  });
}

bool RenderScene::occluded(const Ray &ray) const
{
  float maximum = ray.tMax;
  return m_Impl->geometry->bvh.visit(ray, maximum, true, [&](std::uint32_t index) {
    const auto &instance = m_Impl->geometry->instances[index];
    const Ray local{Math::point(instance.inverse, ray.origin), Math::vector(instance.inverse, ray.direction),
                    ray.tMin, ray.tMax};
    return instance.mesh->bvh.visit(local, maximum, true, [&](std::uint32_t primitive) {
      SurfaceHit ignored;
      return triangle(*instance.mesh->mesh, primitive, local, maximum, ignored);
    });
  });
}

const Material &RenderScene::material(const SurfaceHit &hit) const
{
  return m_Impl->materials.at(hit.instance);
}
std::size_t RenderScene::instanceCount() const noexcept { return m_Impl->materials.size(); }
std::size_t RenderScene::emitterCount() const noexcept { return m_Impl->emitters.size(); }
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
  if (m_Impl->emitters.empty())
    return {};
  const double target =
      std::clamp(static_cast<double>(selection), 0.0, std::nextafter(1.0, 0.0)) * m_Impl->totalWeight;
  const auto it =
      std::upper_bound(m_Impl->emitters.begin(), m_Impl->emitters.end(), target,
                       [](double value, const Emitter &emitter) { return value < emitter.cumulative; });
  const auto &emitter = it == m_Impl->emitters.end() ? m_Impl->emitters.back() : *it;
  const float root = std::sqrt(std::clamp(sample.x, 0.0f, 1.0f));
  const Vec3 weights{1 - root, root * (1 - sample.y), root * sample.y};
  const auto p = emitter.a * weights.x + emitter.b * weights.y + emitter.c * weights.z;
  const auto error = (Math::abs(emitter.a) * weights.x + Math::abs(emitter.b) * weights.y +
                      Math::abs(emitter.c) * weights.z) *
                     (16 * std::numeric_limits<float>::epsilon());
  return {p,
          emitter.normal,
          emitter.emission,
          static_cast<float>(emitter.weight / m_Impl->totalWeight / emitter.area),
          error,
          emitter.twoSided};
}

float RenderScene::emitterPdf(Vec3 reference, const SurfaceHit &hit) const
{
  if (hit.instance >= m_Impl->emitterIndices.size())
    return 0;
  const auto &indices = m_Impl->emitterIndices[hit.instance];
  const auto it = indices.find(hit.primitive);
  if (it == indices.end())
    return 0;
  const auto &emitter = m_Impl->emitters[it->second];
  const auto delta = hit.position - reference;
  const float distance = Math::length(delta);
  if (distance == 0)
    return 0;
  const float cosine = dot(emitter.normal, delta * (-1 / distance));
  if (!emitter.twoSided && cosine <= 0)
    return 0;
  const float absolute = std::abs(cosine);
  return absolute > 0 ? static_cast<float>(emitter.weight / m_Impl->totalWeight / emitter.area) * distance *
                            distance / absolute
                      : 0;
}

} // namespace Jade::Renderer
