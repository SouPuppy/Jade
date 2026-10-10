#pragma once

#include <Jade/Mesh/Vertex.hpp>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Jade {

// Indexed triangles in local coordinates. Normals describe the surface direction.
struct Mesh {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;

  // Side length; centered at the origin, with separate normals for each face.
  static Mesh cube(float size = 1.0f);
  // Wavefront OBJ: positions, normals and convex polygon faces. Missing normals
  // are generated. UVs and materials are not retained. Paths use the working directory.
  static Mesh load(const std::filesystem::path& path);
};

} // namespace Jade
