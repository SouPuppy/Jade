#ifndef JADE_RASTER_COMMON_REFLECTION_GLSL
#define JADE_RASTER_COMMON_REFLECTION_GLSL
#include "ScreenTrace.glsl"
#include "../Materials/Parameters.glsl"
uniform sampler2D u_Source, u_Brdf;
uniform samplerCube u_Probe;
uniform float u_ProbeMip;
uniform bool u_Reflections;
vec3 reflection(vec3 p, vec3 n, vec3 f0)
{
  vec3 v = outgoing(p), d = reflect(-v, n);
  vec2 ab = texture(u_Brdf, vec2(max(dot(n, v), 0), u_Roughness)).rg;
  vec3 weight = f0 * ab.x + ab.y;
  vec3 fallback = textureLod(u_Probe, d, u_Roughness * u_ProbeMip).rgb * weight;
  vec2 at;
  vec3 point;
  if (screenHit(p + n * u_Thickness * .1, d, u_Positions, u_ReflectionSteps, at, point)) {
    float edge = min(min(at.x, at.y), min(1 - at.x, 1 - at.y));
    float confidence = smoothstep(0, .05, edge);
    vec3 color = textureLod(u_Source, at, u_Roughness * u_ProbeMip).rgb * weight;
    return mix(fallback, color, confidence);
  }
  return fallback;
}
#endif
