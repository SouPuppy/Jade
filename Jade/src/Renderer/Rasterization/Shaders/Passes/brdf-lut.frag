#version 330 core
#include "../Materials/GGX.glsl"
#include "../Common/Sampling.glsl"
in vec2 uv;
out vec4 color;
void main()
{
  float nv = max(uv.x, .0001), r = uv.y;
  vec3 v = vec3(sqrt(1 - nv * nv), 0, nv), n = vec3(0, 0, 1);
  vec2 sum = vec2(0);
  for (uint i = 0u; i < 128u; ++i) {
    vec3 h = ggx(vec2(float(i) / 128, radical(i)), n, r), l = normalize(2 * dot(v, h) * h - v);
    float nl = max(l.z, 0), nh = max(h.z, 0), vh = max(dot(v, h), 0);
    if (nl > 0) {
      float a = max(.001, r * r), g = mask(nv, a) * mask(nl, a) * vh / max(nh * nv, 1e-8), f = pow(1 - vh, 5);
      sum += vec2((1 - f) * g, f * g);
    }
  }
  color = vec4(sum / 128, 0, 1);
}
