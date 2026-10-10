#include "Mesh.hpp"

#include "../internal/Mesh/MeshValidation.hpp"
#include "../internal/Math/Validation.hpp"
#include "../internal/Mesh/MeshAccess.hpp"
#include "../Renderer/Common/Math.hpp"

#include <limits>
#include <stdexcept>

namespace Jade {
namespace {
using namespace Renderer;
namespace Math = Renderer::Math;
// Sheared ray-space triangle test. Shared edges use the same calculations.
bool triangle(const MeshData &mesh, std::uint32_t primitive, const Ray &ray, float maximum, SurfaceHit &hit)
{
  const auto &a = mesh.vertices[mesh.indices[static_cast<std::size_t>(primitive) * 3]];
  const auto &b = mesh.vertices[mesh.indices[static_cast<std::size_t>(primitive) * 3 + 1]];
  const auto &c = mesh.vertices[mesh.indices[static_cast<std::size_t>(primitive) * 3 + 2]];
  int kz = 0;
  if (std::abs(ray.direction.y) > std::abs(Detail::Coordinate(ray.direction, kz)))
    kz = 1;
  if (std::abs(ray.direction.z) > std::abs(Detail::Coordinate(ray.direction, kz)))
    kz = 2;
  if (Detail::Coordinate(ray.direction, kz) == 0)
    return false;
  int kx = (kz + 1) % 3, ky = (kx + 1) % 3;
  if (Detail::Coordinate(ray.direction, kz) < 0)
    std::swap(kx, ky);
  const double sx =
      static_cast<double>(Detail::Coordinate(ray.direction, kx)) / Detail::Coordinate(ray.direction, kz);
  const double sy =
      static_cast<double>(Detail::Coordinate(ray.direction, ky)) / Detail::Coordinate(ray.direction, kz);
  const double sz = 1.0 / Detail::Coordinate(ray.direction, kz);
  const Vec3 vertices[3] = {a.position, b.position, c.position};
  double x[3], y[3], z[3];
  for (int i = 0; i < 3; ++i) {
    const double px =
        static_cast<double>(Detail::Coordinate(vertices[i], kx)) - Detail::Coordinate(ray.origin, kx);
    const double py =
        static_cast<double>(Detail::Coordinate(vertices[i], ky)) - Detail::Coordinate(ray.origin, ky);
    const double pz =
        static_cast<double>(Detail::Coordinate(vertices[i], kz)) - Detail::Coordinate(ray.origin, kz);
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

} // namespace

struct Mesh::Impl {
  MeshData data;
  Detail::BVH bvh;
};

Mesh::Mesh() : m_Impl(std::make_shared<Impl>()) {}
const MeshData &Mesh::data() const noexcept { return m_Impl->data; }

Mesh Mesh::compile(MeshData data)
{
  Detail::ValidateMesh(data);
  auto compiled = std::make_shared<Impl>();
  compiled->data = std::move(data);
  const auto &mesh = compiled->data;
  std::vector<Detail::Bounds> bounds(mesh.indices.size() / 3);
  for (std::size_t i = 0; i < bounds.size(); ++i) {
    const auto a = mesh.vertices[mesh.indices[i * 3]].position;
    const auto b = mesh.vertices[mesh.indices[i * 3 + 1]].position;
    const auto c = mesh.vertices[mesh.indices[i * 3 + 2]].position;
    if (Math::length(cross(b - a, c - a)) > 0) {
      bounds[i].add(a);
      bounds[i].add(b);
      bounds[i].add(c);
    }
  }
  compiled->bvh.build(bounds);
  return Mesh(std::move(compiled));
}

MeshData MeshData::triangle(Vec3 a, Vec3 b, Vec3 c)
{
  Detail::Validate(a);
  Detail::Validate(b);
  Detail::Validate(c);
  const double abx = static_cast<double>(b.x) - a.x, aby = static_cast<double>(b.y) - a.y;
  const double abz = static_cast<double>(b.z) - a.z, acx = static_cast<double>(c.x) - a.x;
  const double acy = static_cast<double>(c.y) - a.y, acz = static_cast<double>(c.z) - a.z;
  const double nx = aby * acz - abz * acy, ny = abz * acx - abx * acz, nz = abx * acy - aby * acx;
  const double length = std::hypot(nx, ny, nz);
  Vec3 normal;
  if (length > 0) {
    normal = {static_cast<float>(nx / length), static_cast<float>(ny / length),
              static_cast<float>(nz / length)};
  }
  return {{{a, normal}, {b, normal}, {c, normal}}, {0, 1, 2}};
}

namespace Detail {

void ValidateMesh(const MeshData &mesh)
{
  // Triangle and vertex identities use unsigned 32-bit indices.
  if (mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
      mesh.indices.size() / 3 > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("Mesh exceeds the supported vertex or index count.");
  }
  if (mesh.indices.size() % 3 != 0) {
    throw std::invalid_argument("Mesh indices must describe complete primitives.");
  }
  for (const auto &vertex : mesh.vertices) {
    Validate(vertex.position);
    Validate(vertex.normal);
  }
  for (const auto index : mesh.indices) {
    if (index >= mesh.vertices.size()) {
      throw std::invalid_argument("Mesh index is outside the vertex array.");
    }
  }
}

} // namespace Detail
} // namespace Jade

namespace Jade::Detail {
const BVH &MeshAccess::acceleration(const Mesh &mesh) { return mesh.m_Impl->bvh; }
const void *MeshAccess::identity(const Mesh &mesh) { return mesh.m_Impl.get(); }
bool MeshAccess::intersect(const Mesh &mesh, const Renderer::Ray &ray, Renderer::SurfaceHit &hit)
{
  float maximum = ray.tMax;
  return mesh.m_Impl->bvh.visit(ray, maximum, false, [&](std::uint32_t primitive) {
    Renderer::SurfaceHit candidate;
    if (!triangle(mesh.data(), primitive, ray, maximum, candidate))
      return false;
    maximum = candidate.t;
    candidate.frontFace = dot(ray.direction, candidate.geometricNormal) < 0;
    hit = candidate;
    return true;
  });
}
bool MeshAccess::occluded(const Mesh &mesh, const Renderer::Ray &ray)
{
  float maximum = ray.tMax;
  return mesh.m_Impl->bvh.visit(ray, maximum, true, [&](std::uint32_t primitive) {
    Renderer::SurfaceHit ignored;
    return triangle(mesh.data(), primitive, ray, maximum, ignored);
  });
}
} // namespace Jade::Detail
