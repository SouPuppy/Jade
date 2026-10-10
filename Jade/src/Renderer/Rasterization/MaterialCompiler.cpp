#include "MaterialCompiler.hpp"

#include <stdexcept>

namespace Jade::Detail {
RasterMaterial compileRasterMaterial(const Material &material)
{
  validateMaterial(material);
  RasterClosure closure;
  switch (material.model) {
  case MaterialModel::Diffuse:
    closure = RasterClosure::Lambert;
    break;
  case MaterialModel::Metal:
    closure = RasterClosure::Conductor;
    break;
  case MaterialModel::Dielectric:
    closure = RasterClosure::Dielectric;
    break;
  default:
    throw std::invalid_argument("Unknown raster material model.");
  }
  return {closure,      material.baseColor,       material.emission, material.roughness,
          material.ior, material.twoSidedEmission};
}
} // namespace Jade::Detail
