#pragma once
#include <Jade/Materials/Material.hpp>

namespace Jade::Detail {
// Private raster encoding; independent of MaterialModel's enum ordering.
enum class RasterClosure { Lambert = 0, Conductor = 1, Dielectric = 2 };
struct RasterMaterial {
  RasterClosure closure;
  Renderer::LinearRGB baseColor, emission;
  float roughness, ior;
  bool twoSidedEmission;
  bool transmission() const noexcept { return closure == RasterClosure::Dielectric; }
  bool reflection() const noexcept { return closure != RasterClosure::Lambert; }
};

RasterMaterial compileRasterMaterial(const Material &material);
} // namespace Jade::Detail
