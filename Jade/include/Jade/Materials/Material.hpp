#pragma once

#include <Jade/Renderer/Common/LinearRGB.hpp>

namespace Jade {

enum class MaterialModel { Diffuse, Metal, Dielectric };

// Physical parameters only; each backend owns its execution representation.
// For Metal, baseColor is normal-incidence reflectance. For Dielectric it is
// a surface transmission tint (not volumetric absorption).
struct Material {
  MaterialModel model = MaterialModel::Diffuse;
  Renderer::LinearRGB baseColor{0.8f, 0.8f, 0.8f};
  float roughness = 0.5f;
  float ior = 1.5f;
  Renderer::LinearRGB emission;
  bool twoSidedEmission = false;
  bool operator==(const Material &) const = default;
};

void validateMaterial(const Material &material);

} // namespace Jade
