#ifndef JADE_RASTER_MATERIALS_BSDF_GLSL
#define JADE_RASTER_MATERIALS_BSDF_GLSL
#include "Parameters.glsl"
#include "Diffuse.glsl"
#include "Metal.glsl"
#include "Dielectric.glsl"
#include "GGX.glsl"
vec3 brdf(vec3 n, vec3 v, vec3 l)
{
  float nv = max(dot(n, v), 0), nl = max(dot(n, l), 0);
  if (nv <= 0 || nl <= 0)
    return vec3(0);
  if (u_ModelKind == MATERIAL_LAMBERT)
    return lambert(u_BaseColor);
  vec3 h = normalize(v + l);
  float nh = max(dot(n, h), 0), vh = max(dot(v, h), 0), a = max(.001, u_Roughness * u_Roughness), aa = a * a;
  float d = aa / (PI * pow(nh * nh * (aa - 1) + 1, 2));
  vec3 f = u_ModelKind == MATERIAL_CONDUCTOR ? fresnel(u_BaseColor, vh) : vec3(dielectric(vh, u_Ior));
  return f * d * mask(nv, a) * mask(nl, a) / (4 * nv * nl);
}
#endif
