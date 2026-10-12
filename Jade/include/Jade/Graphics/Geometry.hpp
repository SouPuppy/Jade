#pragma once

#include <Jade/Assets/AssetHandle.hpp>
#include <Jade/Math/Vector.hpp>

namespace Jade {

enum class GeometryKind : std::uint32_t { Mesh, Sphere, Plane };
// Analytic primitives use cached tessellation for raster/debug and exact rays.
struct Geometry {
  GeometryKind kind = GeometryKind::Mesh;
  MeshHandle resource{};
  float radius = 1;
  Vec2 size{1, 1};
  Geometry() = default;
  explicit Geometry(MeshHandle mesh) : resource(mesh) {}
  static Geometry mesh(MeshHandle handle) { return Geometry(handle); }
  static Geometry sphere(float radius = 1) {
    Geometry geometry; geometry.kind = GeometryKind::Sphere; geometry.radius = radius; return geometry;
  }
  static Geometry plane(Vec2 size = {1, 1}) {
    Geometry geometry; geometry.kind = GeometryKind::Plane; geometry.size = size; return geometry;
  }
};

} // namespace Jade
