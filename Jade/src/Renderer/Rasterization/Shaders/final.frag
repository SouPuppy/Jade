#version 330 core
#include "shared.glsl"
in vec3 worldPosition,worldNormal;
uniform vec2 u_Size;
out vec4 color;
void main(){vec2 uv=gl_FragCoord.xy/u_Size;vec3 source=texture(u_Source,uv).rgb;
 if(u_ModelKind==1&&u_Reflections)source=emitted()+reflection(worldPosition,surfaceNormal(worldPosition,worldNormal),u_BaseColor);
 color=vec4(source,1);}
