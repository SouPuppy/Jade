#version 330 core
#include "shared.glsl"
in vec3 worldPosition,worldNormal;
uniform vec2 u_Size;
uniform bool u_Capture;
out vec4 color;
void main(){vec2 uv=gl_FragCoord.xy/u_Size;vec3 v=outgoing(worldPosition),n=surfaceNormal(worldPosition,worldNormal);float eta=gl_FrontFacing?u_Ior:1/u_Ior;float f=dielectric(dot(n,v),eta);
 vec3 reflected=abs(u_Ior-1)<1e-6?vec3(0):u_Reflections?reflection(worldPosition,n,vec3(pow((u_Ior-1)/(u_Ior+1),2))):lighting(worldPosition,n);
 vec3 transmitted=texture(u_Source,uv).rgb;vec3 direction=refract(-v,n,1/eta);
 if(dot(direction,direction)==0)f=1;
 else if(u_Refractions&&!u_Capture&&abs(u_Ior-1)>1e-6){vec2 exitUv;vec3 exitPoint;vec3 start=worldPosition+direction*u_Thickness*.1;
   if(!gl_FrontFacing){exitPoint=start;exitUv=uv;}
   bool exited=!gl_FrontFacing||screenHit(start,direction,u_BackPosition,u_RefractionSteps,exitUv,exitPoint);
   if(exited){vec3 exitN=normalize(texture(u_BackNormal,exitUv).xyz);vec3 outDirection=gl_FrontFacing?refract(direction,-exitN,u_Ior):direction;
     if(dot(outDirection,outDirection)==0)f=1;else {vec2 at;vec3 point;if(screenHit(exitPoint+outDirection*u_Thickness*.1,outDirection,u_Positions,u_RefractionSteps,at,point))transmitted=textureLod(u_Source,at,u_Roughness*u_ProbeMip).rgb;}}
 }
 if(f>=1&&u_Reflections)reflected=reflection(worldPosition,n,vec3(1));
 color=vec4(emitted()+reflected+(1-f)*u_BaseColor*transmitted,1);
}
