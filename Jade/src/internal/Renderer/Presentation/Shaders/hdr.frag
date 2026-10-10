#version 330 core
in vec2 uv;
uniform sampler2D u_Image;
uniform float u_Exposure;
out vec4 fragmentColor;
vec3 encode(vec3 c)
{
  return mix(12.92 * c, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
void main()
{
  vec3 c = max(texture(u_Image, vec2(uv.x, 1.0 - uv.y)).rgb, vec3(0));
  vec3 mapped = c / (c + vec3(exp2(-u_Exposure)));
  fragmentColor = vec4(encode(mapped), 1);
}
