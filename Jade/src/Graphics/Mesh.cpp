#include "Mesh.hpp"

#include "../internal/Mesh/MeshValidation.hpp"
#include "../internal/Math/Validation.hpp"

#include <limits>
#include <stdexcept>

namespace Jade {

Mesh Mesh::line(Vec3 start, Vec3 end)
{
  Detail::Validate(start);
  Detail::Validate(end);
  return {{{start, {}}, {end, {}}}, {0, 1}, MeshTopology::Lines};
}

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
  // DrawElements takes a signed 32-bit count, and indices address 32-bit vertices.
  if (mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max()
      || mesh.indices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
    throw std::invalid_argument("Mesh exceeds the supported vertex or index count.");
  }
  if (mesh.topology != MeshTopology::Lines && mesh.topology != MeshTopology::Triangles) {
    throw std::invalid_argument("Unknown mesh topology.");
  }
  const std::size_t group = mesh.topology == MeshTopology::Lines ? 2 : 3;
  if (mesh.indices.size() % group != 0) {
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
