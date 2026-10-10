#include "Mesh.hpp"

#include "../internal/Mesh/MeshValidation.hpp"
#include "../internal/Mesh/ObjLoader.hpp"
#include "../internal/Math/Validation.hpp"

#include <limits>
#include <stdexcept>

namespace Jade {

Mesh Mesh::load(const std::filesystem::path& path)
{
  return Detail::LoadObj(path);
}

namespace Detail {

void ValidateMesh(const Mesh& mesh)
{
  // DrawElements takes a signed 32-bit count, and indices address 32-bit vertices.
  if (mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max()
      || mesh.indices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
    throw std::invalid_argument("Mesh exceeds the supported vertex or index count.");
  }
  if (mesh.indices.size() % 3 != 0) {
    throw std::invalid_argument("Mesh indices must describe complete triangles.");
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
