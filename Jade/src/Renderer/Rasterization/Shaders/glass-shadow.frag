#version 330 core
#include "shared.glsl"
in vec3 worldPosition, worldNormal;
uniform int u_SkipPrimitive, u_PrimitiveBase;
out vec4 color;
void main()
{
  if (gl_PrimitiveID + u_PrimitiveBase == u_SkipPrimitive)
    discard;
  vec3 n = surfaceNormal(worldPosition, worldNormal), v = normalize(u_Eye - worldPosition);
  color = vec4(u_BaseColor * (1 - dielectric(abs(dot(n, v)), u_Ior)), 1);
}
