#version 330 core
#include "shared.glsl"
in vec3 worldPosition,worldNormal;
out vec4 color;
void main(){vec3 n=normalize(worldNormal),v=normalize(u_Eye-worldPosition);color=vec4(u_BaseColor*(1-dielectric(abs(dot(n,v)),u_Ior)),1);}
