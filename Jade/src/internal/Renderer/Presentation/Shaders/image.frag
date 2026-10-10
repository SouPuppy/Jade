#version 330 core
in vec2 uv;
uniform sampler2D u_Image;
out vec4 fragmentColor;
void main() { fragmentColor = texture(u_Image, uv); }
