#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D hdr;
uniform float exposureEV;
uniform int normalOutput;
vec3 srgb(vec3 x) { return mix(12.92*x,1.055*pow(max(x,vec3(0)),vec3(1.0/2.4))-.055,step(vec3(.0031308),x)); }
void main() {
  vec3 value=texture(hdr,vec2(uv.x,1.0-uv.y)).rgb;
  if(normalOutput==0) { value*=exp2(exposureEV); value=value/(1.0+value); value=srgb(value); }
  color=vec4(value,1);
}
