#version 330 core
in vec3 worldPosition,worldNormal;
layout(location=0) out vec4 position;
layout(location=1) out vec4 normal;
void main(){position=vec4(worldPosition,1);normal=vec4(normalize(worldNormal),1);}
