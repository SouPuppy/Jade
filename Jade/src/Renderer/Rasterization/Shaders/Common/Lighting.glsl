#ifndef JADE_RASTER_COMMON_LIGHTING_GLSL
#define JADE_RASTER_COMMON_LIGHTING_GLSL
#include "Frame.glsl"
#include "../Materials/BSDF.glsl"
uniform samplerBuffer u_Lights;
uniform int u_LightCount;
uniform bool u_Shadows;
uniform sampler2DArray u_ShadowDepth, u_GlassDepth, u_GlassTint;
uniform vec2 u_ShadowTexel;
int cubeFace(vec3 d)
{
  vec3 a = abs(d);
  if (a.x >= a.y && a.x >= a.z)
    return d.x >= 0 ? 0 : 1;
  if (a.y >= a.z)
    return d.y >= 0 ? 2 : 3;
  return d.z >= 0 ? 4 : 5;
}
vec3 visibility(int i, vec3 p)
{
  if (!u_Shadows)
    return vec3(1);
  int face = cubeFace(p - texelFetch(u_Lights, i * 27).xyz), o = i * 27 + 3 + face * 4;
  mat4 m = mat4(texelFetch(u_Lights, o), texelFetch(u_Lights, o + 1), texelFetch(u_Lights, o + 2),
                texelFetch(u_Lights, o + 3));
  vec4 q = m * vec4(p, 1);
  vec3 t = q.xyz / q.w * .5 + .5;
  float bias = max(0.00002, 0.0002 * (1 - t.z));
  vec3 total = vec3(0);
  for (int k = 0; k < 4; ++k) {
    vec2 offset = vec2((k & 1) == 0 ? -.5 : .5, (k & 2) == 0 ? -.5 : .5) * u_ShadowTexel;
    vec3 at = vec3(t.xy + offset, float(i * 6 + face));
    float z = texture(u_ShadowDepth, at).r;
    if (t.z - bias <= z) {
      float g = texture(u_GlassDepth, at).r;
      total += g < t.z - bias ? texture(u_GlassTint, at).rgb : vec3(1);
    }
  }
  return total * .25;
}
vec3 lighting(vec3 p, vec3 n)
{
  vec3 v = outgoing(p), result = vec3(0);
  for (int i = 0; i < u_LightCount; ++i) {
    vec3 lp = texelFetch(u_Lights, i * 27).xyz;
    vec4 ln = texelFetch(u_Lights, i * 27 + 1);
    vec3 flux = texelFetch(u_Lights, i * 27 + 2).rgb;
    vec3 delta = lp - p;
    float r2 = dot(delta, delta);
    if (r2 <= 1e-16)
      continue;
    vec3 l = delta * inversesqrt(r2);
    float cn = dot(ln.xyz, -l);
    cn = ln.w > 0 ? abs(cn) : max(cn, 0);
    float ns = max(dot(n, l), 0);
    if (cn > 0 && ns > 0)
      result += flux * brdf(n, v, l) * cn * ns / r2 * visibility(i, p);
  }
  return result;
}
#endif
