#version 330 core
#include "../Materials/GGX.glsl"
#include "../Common/Sampling.glsl"
uniform samplerCube u_Probe;
uniform float u_Roughness;
in vec2 uv;
uniform vec3 u_Direction, u_Right, u_Up;
out vec4 color;
void main()
{
  vec3 n = normalize(u_Direction + u_Right * (uv.x * 2 - 1) + u_Up * (uv.y * 2 - 1));
  vec3 total = vec3(0);
  float weight = 0;
  for (uint i = 0u; i < 64u; ++i) {
    vec3 h = ggx(vec2(float(i) / 64, radical(i)), n, u_Roughness);
    vec3 l = normalize(2 * dot(n, h) * h - n);
    float c = max(dot(n, l), 0);
    if (c > 0) {
      total += textureLod(u_Probe, l, 0).rgb * c;
      weight += c;
    }
  }
  color = vec4(total / max(weight, 1e-8), 1);
}
