#include "Material.hpp"

#include <cmath>
#include <stdexcept>

namespace Jade {
void validateMaterial(const Material &material)
{
  if (material.model != MaterialModel::Diffuse && material.model != MaterialModel::Metal &&
      material.model != MaterialModel::Dielectric)
    throw std::invalid_argument("Unknown material model.");
  for (const auto v : {material.baseColor.r, material.baseColor.g, material.baseColor.b})
    if (!std::isfinite(v) || v < 0 || v > 1)
      throw std::invalid_argument("Material baseColor must be within [0, 1].");
  for (const auto v : {material.emission.r, material.emission.g, material.emission.b})
    if (!std::isfinite(v) || v < 0)
      throw std::invalid_argument("Emission must be finite and nonnegative.");
  if (!std::isfinite(material.roughness) || material.roughness < 0 || material.roughness > 1)
    throw std::invalid_argument("Roughness must be within [0, 1].");
  if (!std::isfinite(material.ior) || material.ior < 1 || material.ior > 10)
    throw std::invalid_argument("IOR must be within [1, 10].");
}
} // namespace Jade
