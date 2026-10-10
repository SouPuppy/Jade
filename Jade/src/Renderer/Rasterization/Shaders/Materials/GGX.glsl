#ifndef JADE_RASTER_MATERIALS_GGX_GLSL
#define JADE_RASTER_MATERIALS_GGX_GLSL
#include "../Common/Constants.glsl"
float mask(float c, float a)
{
  if (c <= 0)
    return 0.0;
  return 2 / (1 + sqrt(1 + a * a * (1 - c * c) / (c * c)));
}
vec3 ggx(vec2 xi, vec3 n, float roughness)
{
  float a = max(.001, roughness * roughness), phi = 2 * PI * xi.x,
        c = sqrt((1 - xi.y) / (1 + (a * a - 1) * xi.y)), s = sqrt(max(0, 1 - c * c));
  vec3 h = vec3(cos(phi) * s, sin(phi) * s, c);
  vec3 up = abs(n.z) < .999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
  vec3 t = normalize(cross(up, n));
  return normalize(t * h.x + cross(n, t) * h.y + n * h.z);
}
#endif
