#pragma once
#include <Jade/Graphics/Vertex.hpp>
#include <cstdint>
#include <vector>
namespace Jade {
struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  static MeshData cube(float size = 1);
  static MeshData sphere(float radius = 0.5f, std::uint32_t segments = 32, std::uint32_t rings = 16);
  static MeshData triangle(Vec3 a, Vec3 b, Vec3 c);
};
} // namespace Jade
