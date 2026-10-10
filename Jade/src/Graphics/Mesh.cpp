#include "Mesh.hpp"

#include "../internal/Mesh/MeshValidation.hpp"
#include "../internal/Math/Validation.hpp"

#include <limits>
#include <stdexcept>

namespace Jade {

Mesh Mesh::triangle(Vec3 a, Vec3 b, Vec3 c)
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
    normal = {static_cast<float>(nx / length), static_cast<float>(ny / length), static_cast<float>(nz / length)};
  }
  return {{{a, normal}, {b, normal}, {c, normal}}, {0, 1, 2}};
}

namespace Detail {

void ValidateMesh(const Mesh& mesh)
{
  // Triangle and vertex identities use unsigned 32-bit indices.
  if (mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max()
      || mesh.indices.size() / 3 > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("Mesh exceeds the supported vertex or index count.");
  }
  if (mesh.indices.size() % 3 != 0) {
    throw std::invalid_argument("Mesh indices must describe complete primitives.");
  }
  for (const auto& vertex : mesh.vertices) {
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
