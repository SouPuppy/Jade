#version 330 core
in vec2 uv;
uniform sampler2D u_Image;
uniform int u_Linear, u_Data;
uniform float u_Exposure;
out vec4 fragmentColor;
void main() {
  vec3 color = texture(u_Image, uv).rgb;
  if (u_Linear != 0 && u_Data == 0) {
    // Equivalent to exposed/(1+exposed), without overflowing HDR * exposure.
    color = color / (vec3(exp2(-u_Exposure)) + color);
    color = mix(12.92 * color, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055, greaterThan(color, vec3(0.0031308)));
  }
  fragmentColor = vec4(clamp(color, 0.0, 1.0), 1);
}
