#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
uniform mat4 model, viewProjection;
out vec3 worldPosition, worldNormal;
out vec2 uv;
void main() {
  worldPosition = (model * vec4(position,1)).xyz;
  worldNormal = transpose(inverse(mat3(model))) * normal;
  uv = vec2(position.x + .5, 1.0-position.y);
  gl_Position = viewProjection * vec4(worldPosition,1);
}
