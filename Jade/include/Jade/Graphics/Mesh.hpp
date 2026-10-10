#pragma once

#include <Jade/Graphics/Vertex.hpp>

#include <cstdint>
#include <vector>

namespace Jade {

enum class MeshTopology { Lines, Triangles };

// Indexed geometry in local 3D coordinates. Assets owns immutable mesh snapshots.
struct Mesh {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  MeshTopology topology = MeshTopology::Triangles;

  // Side length; centered at the origin, with separate normals for each face.
  static Mesh cube(float size = 1.0f);
  static Mesh line(Vec3 start, Vec3 end);
  static Mesh triangle(Vec3 a, Vec3 b, Vec3 c);
};

} // namespace Jade
