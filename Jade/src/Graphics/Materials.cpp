#include "Materials.hpp"
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
