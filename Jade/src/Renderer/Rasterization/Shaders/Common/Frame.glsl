#ifndef JADE_RASTER_COMMON_FRAME_GLSL
#define JADE_RASTER_COMMON_FRAME_GLSL
uniform vec3 u_Eye, u_Forward;
uniform bool u_Perspective;
uniform mat4 u_VP;
vec3 outgoing(vec3 p) { return u_Perspective ? normalize(u_Eye - p) : -u_Forward; }
vec3 surfaceNormal(vec3 p, vec3 normal)
{
  vec3 n = length(normal) > 1e-8 ? normalize(normal) : normalize(cross(dFdx(p), dFdy(p)));
  return dot(n, outgoing(p)) < 0 ? -n : n;
}
#endif
