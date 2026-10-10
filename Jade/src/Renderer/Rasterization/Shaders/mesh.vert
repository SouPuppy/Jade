#version 330 core
layout(location=0) in vec3 a_Position;
layout(location=1) in vec3 a_Normal;
uniform mat4 u_Model,u_Normal,u_VP;
out vec3 worldPosition,worldNormal;
void main(){vec4 p=u_Model*vec4(a_Position,1);worldPosition=p.xyz;worldNormal=(u_Normal*vec4(a_Normal,0)).xyz;gl_Position=u_VP*p;}
