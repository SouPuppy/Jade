#include "Sphere.hpp"

#include "../../internal/Math/Validation.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace Jade {

Mesh Mesh::sphere(float radius, std::uint32_t segments, std::uint32_t rings)
{
  Detail::ValidatePositive(radius);
  if (segments < 3 || rings < 2) {
    throw std::invalid_argument("Sphere requires at least 3 longitude and 2 latitude segments.");
  }
  const auto ringVertices = std::uint64_t{segments} * (rings - 1);
  // Check before multiplying the index count or allocating geometry.
  if (ringVertices + 2 > std::numeric_limits<std::uint32_t>::max()
      || ringVertices > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) / 6) {
    throw std::invalid_argument("Sphere exceeds the supported vertex or index count.");
  }

  Mesh mesh;
  mesh.vertices.reserve(static_cast<std::size_t>(ringVertices + 2));
  mesh.indices.reserve(static_cast<std::size_t>(6 * ringVertices));
  mesh.vertices.push_back({{0, radius, 0}, {0, 1, 0}});
  for (std::uint32_t latitude = 1; latitude < rings; ++latitude) {
    const double angle = std::numbers::pi * latitude / rings;
    const double height = std::cos(angle), extent = std::sin(angle);
    for (std::uint32_t longitude = 0; longitude < segments; ++longitude) {
      const double azimuth = 2 * std::numbers::pi * longitude / segments;
      const Vec3 normal{static_cast<float>(extent * std::cos(azimuth)),
        static_cast<float>(height), static_cast<float>(extent * std::sin(azimuth))};
      mesh.vertices.push_back({normal * radius, normal});
    }
  }
  const auto south = static_cast<std::uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({{0, -radius, 0}, {0, -1, 0}});

  const auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    mesh.indices.insert(mesh.indices.end(), {a, b, c});
  };
  // Shared poles and wrapped longitude indices avoid cracks and collapsed pole triangles.
  for (std::uint32_t longitude = 0; longitude < segments; ++longitude) {
    const auto next = (longitude + 1) % segments;
    triangle(0, 1 + next, 1 + longitude);
    for (std::uint32_t latitude = 0; latitude < rings - 2; ++latitude) {
      const auto a = 1 + latitude * segments + longitude;
      const auto b = 1 + latitude * segments + next;
      const auto c = a + segments;
      const auto d = b + segments;
      triangle(a, b, c);
      triangle(b, d, c);
    }
    const auto lastRing = 1 + (rings - 2) * segments;
    triangle(lastRing + longitude, lastRing + next, south);
  }
  return mesh;
}

} // namespace Jade
