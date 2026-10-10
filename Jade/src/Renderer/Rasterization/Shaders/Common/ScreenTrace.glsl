#ifndef JADE_RASTER_COMMON_SCREENTRACE_GLSL
#define JADE_RASTER_COMMON_SCREENTRACE_GLSL
#include "Frame.glsl"
uniform sampler2D u_Positions;
uniform float u_Range, u_Thickness;
uniform int u_ReflectionSteps, u_RefractionSteps;
// Search only current camera information. The alpha channel marks valid surfaces.
bool screenHit(vec3 origin, vec3 direction, sampler2D positions, int steps, out vec2 hit, out vec3 point)
{
  float lastT = 0, lastDelta = -1e30;
  vec2 lastUv = vec2(0);
  for (int i = 1; i <= 512; ++i) {
    if (i > steps)
      break;
    float t = u_Range * float(i) / float(steps);
    vec3 p = origin + direction * t;
    vec4 clip = u_VP * vec4(p, 1);
    if (clip.w <= 0)
      break;
    vec3 ndc = clip.xyz / clip.w;
    vec2 at = ndc.xy * .5 + .5;
    if (any(lessThan(at, vec2(0))) || any(greaterThan(at, vec2(1))) || ndc.z > 1)
      break;
    vec4 surface = texture(positions, at);
    float delta = dot(p - surface.xyz, u_Forward);
    if (surface.w > 0 && delta >= 0 && lastDelta < 0) {
      float lo = lastT, hi = t;
      vec4 refined = surface;
      for (int j = 0; j < 6; ++j) {
        float mid = (lo + hi) * .5;
        vec3 a = origin + direction * mid;
        vec4 c = u_VP * vec4(a, 1);
        hit = c.xy / c.w * .5 + .5;
        refined = texture(positions, hit);
        if (refined.w > 0 && dot(a - refined.xyz, u_Forward) > 0)
          hi = mid;
        else
          lo = mid;
      }
      vec3 candidate = origin + direction * hi;
      if (refined.w > 0 && abs(dot(candidate - refined.xyz, u_Forward)) <= u_Thickness) {
        point = refined.xyz;
        return true;
      }
    }
    lastT = t;
    lastDelta = surface.w > 0 ? delta : -1e30;
    lastUv = at;
  }
  hit = lastUv;
  point = origin;
  return false;
}
#endif
