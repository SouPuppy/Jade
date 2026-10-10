#pragma once

#include <Jade/Graphics/Vertex.hpp>

#include <cstdint>
#include <vector>

namespace Jade {

// Indexed geometry in local 3D coordinates. Assets owns immutable mesh snapshots.
struct Mesh {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;

  // Side length; centered at the origin, with separate normals for each face.
  static Mesh cube(float size = 1.0f);
  // Radius; centered at the origin with smooth outward normals and Y-up poles.
  // segments divides longitude (>= 3); rings divides pole-to-pole latitude (>= 2).
  static Mesh sphere(float radius = 0.5f, std::uint32_t segments = 32, std::uint32_t rings = 16);
  static Mesh triangle(Vec3 a, Vec3 b, Vec3 c);
};

} // namespace Jade
