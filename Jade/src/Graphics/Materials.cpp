#include "Materials.hpp"
#include <cmath>
#include <stdexcept>
namespace Jade::Materials {
Material Diffuse(Renderer::LinearRGB color)
{
  Material result;
  result.baseColor = color;
  return result;
}
Material Metal(Renderer::LinearRGB reflectance, float roughness)
{
  auto result = Diffuse(reflectance);
  result.model = MaterialModel::Metal;
  result.roughness = roughness;
  return result;
}
Material Gold(float roughness) { return Metal({0.95f, 0.7f, 0.3f}, roughness); }
Material Silver(float roughness) { return Metal({0.95f, 0.93f, 0.88f}, roughness); }
Material Copper(float roughness) { return Metal({0.95f, 0.64f, 0.54f}, roughness); }
Material Glass(float roughness, float ior, Renderer::LinearRGB tint)
{
  auto result = Diffuse(tint);
  result.model = MaterialModel::Dielectric;
  result.roughness = roughness;
  result.ior = ior;
  return result;
}
Material Emissive(Renderer::LinearRGB radiance, bool twoSided)
{
  auto result = Diffuse({});
  result.emission = radiance;
  result.twoSidedEmission = twoSided;
  return result;
}
} // namespace Jade::Materials

namespace Jade::Renderer {
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

}
