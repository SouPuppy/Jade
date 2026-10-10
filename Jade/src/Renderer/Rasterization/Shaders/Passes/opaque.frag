#version 330 core
#include "../Common/Lighting.glsl"
in vec3 worldPosition, worldNormal;
layout(location = 0) out vec4 radiance;
layout(location = 1) out vec4 position;
layout(location = 2) out vec4 normal;
void main()
{
  vec3 n = surfaceNormal(worldPosition, worldNormal);
  radiance = vec4(lighting(worldPosition, n) + emitted(), 1);
  position = vec4(worldPosition, 1);
  normal = vec4(n, u_Roughness);
}
