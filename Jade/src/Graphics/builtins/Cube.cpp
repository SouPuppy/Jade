#include "Cube.hpp"

#include "../../internal/Math/Validation.hpp"

#include <array>

namespace Jade {

Mesh Mesh::cube(float size)
{
  Detail::ValidatePositive(size);
  const float h = size / 2.0f;
  if (h == 0.0f) {
    throw std::invalid_argument("Cube size is too small to represent its half extent.");
  }
  const std::array<Vec3, 8> positions{{
    {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h},
    {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}
  }};
  constexpr std::array<std::array<std::size_t, 4>, 6> faces{{
    {{0, 1, 2, 3}}, {{5, 4, 7, 6}}, {{1, 5, 6, 2}},
    {{4, 0, 3, 7}}, {{3, 2, 6, 7}}, {{4, 5, 1, 0}}
  }};
  constexpr std::array<Vec3, 6> normals{{
    {0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}
  }};
  Mesh mesh;
  mesh.vertices.reserve(24);
  mesh.indices.reserve(36);
  for (std::size_t face = 0; face < faces.size(); ++face) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const auto corner : faces[face]) {
      mesh.vertices.push_back({positions[corner], normals[face]});
    }
    for (const auto offset : {0u, 1u, 2u, 0u, 2u, 3u}) {
      mesh.indices.push_back(base + offset);
    }
  }
  return mesh;
}

} // namespace Jade
