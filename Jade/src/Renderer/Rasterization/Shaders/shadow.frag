#version 330 core
uniform int u_SkipPrimitive, u_PrimitiveBase;
void main()
{
  if (gl_PrimitiveID + u_PrimitiveBase == u_SkipPrimitive)
    discard;
}
