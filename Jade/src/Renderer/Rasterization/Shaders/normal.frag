#version 330 core
in vec3 worldPosition, worldNormal;
layout(location = 0) out vec4 position;
layout(location = 1) out vec4 normal;
void main()
{
  position = vec4(worldPosition, 1);
  vec3 n = length(worldNormal) > 1e-8 ? normalize(worldNormal)
                                    : normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));
  if (length(worldNormal) <= 1e-8 && !gl_FrontFacing)
    n = -n;
  normal = vec4(n, 1);
}
