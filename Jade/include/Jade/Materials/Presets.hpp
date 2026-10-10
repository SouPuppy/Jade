#pragma once
#include <Jade/Materials/Material.hpp>
namespace Jade::Materials {
Material Diffuse(Renderer::LinearRGB color = {0.8f, 0.8f, 0.8f});
Material Metal(Renderer::LinearRGB reflectance, float roughness = 0.18f);
Material Gold(float roughness = 0.18f);
Material Silver(float roughness = 0.18f);
Material Copper(float roughness = 0.18f);
Material Glass(float roughness = 0, float ior = 1.5f, Renderer::LinearRGB tint = {1, 1, 1});
Material Emissive(Renderer::LinearRGB radiance = {10, 10, 10}, bool twoSided = false);
} // namespace Jade::Materials
