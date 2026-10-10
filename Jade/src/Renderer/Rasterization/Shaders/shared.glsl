const float PI=3.141592653589793;
uniform vec3 u_Eye,u_Forward;
uniform bool u_Perspective;
uniform vec3 u_BaseColor,u_Emission;
uniform int u_ModelKind;
uniform float u_Roughness,u_Ior;
uniform bool u_TwoSided;
uniform samplerBuffer u_Lights;
uniform int u_LightCount;
uniform bool u_Shadows;
uniform sampler2DArray u_ShadowDepth,u_GlassDepth,u_GlassTint;
uniform vec2 u_ShadowTexel;
uniform mat4 u_VP;
uniform sampler2D u_Positions,u_Source,u_BackPosition,u_BackNormal,u_Brdf;
uniform samplerCube u_Probe;
uniform float u_ProbeMip,u_Range,u_Thickness;
uniform int u_ReflectionSteps,u_RefractionSteps;
uniform bool u_Reflections,u_Refractions;
vec3 outgoing(vec3 p){return u_Perspective?normalize(u_Eye-p):-u_Forward;}
vec3 surfaceNormal(vec3 p,vec3 normal){vec3 n=length(normal)>1e-8?normalize(normal):normalize(cross(dFdx(p),dFdy(p)));return dot(n,outgoing(p))<0?-n:n;}
float dielectric(float c,float eta){c=clamp(abs(c),0,1);float sin2=(1-c*c)/(eta*eta);if(sin2>=1)return 1.0;float ct=sqrt(1-sin2);float a=(eta*c-ct)/(eta*c+ct),b=(c-eta*ct)/(c+eta*ct);return .5*(a*a+b*b);}
vec3 fresnel(vec3 f0,float c){return mix(f0,vec3(1),pow(clamp(1-c,0,1),5));}
float mask(float c,float a){if(c<=0)return 0.0;return 2/(1+sqrt(1+a*a*(1-c*c)/(c*c)));}
vec3 brdf(vec3 n,vec3 v,vec3 l){float nv=max(dot(n,v),0),nl=max(dot(n,l),0);if(nv<=0||nl<=0)return vec3(0);if(u_ModelKind==0)return u_BaseColor/PI;
 vec3 h=normalize(v+l);float nh=max(dot(n,h),0),vh=max(dot(v,h),0),a=max(.001,u_Roughness*u_Roughness),aa=a*a;
 float d=aa/(PI*pow(nh*nh*(aa-1)+1,2));vec3 f=u_ModelKind==1?fresnel(u_BaseColor,vh):vec3(dielectric(vh,u_Ior));return f*d*mask(nv,a)*mask(nl,a)/(4*nv*nl);}
int cubeFace(vec3 d){vec3 a=abs(d);if(a.x>=a.y&&a.x>=a.z)return d.x>=0?0:1;if(a.y>=a.z)return d.y>=0?2:3;return d.z>=0?4:5;}
vec3 visibility(int i,vec3 p){if(!u_Shadows)return vec3(1);int face=cubeFace(p-texelFetch(u_Lights,i*27).xyz),o=i*27+3+face*4;
 mat4 m=mat4(texelFetch(u_Lights,o),texelFetch(u_Lights,o+1),texelFetch(u_Lights,o+2),texelFetch(u_Lights,o+3));vec4 q=m*vec4(p,1);vec3 t=q.xyz/q.w*.5+.5;
 float bias=max(0.00002,0.0002*(1-t.z));vec3 total=vec3(0);for(int k=0;k<4;++k){vec2 offset=vec2((k&1)==0?-.5:.5,(k&2)==0?-.5:.5)*u_ShadowTexel;vec3 at=vec3(t.xy+offset,float(i*6+face));
 float z=texture(u_ShadowDepth,at).r;if(t.z-bias<=z){float g=texture(u_GlassDepth,at).r;total+=g<t.z-bias?texture(u_GlassTint,at).rgb:vec3(1);}}return total*.25;}
vec3 lighting(vec3 p,vec3 n){vec3 v=outgoing(p),result=vec3(0);for(int i=0;i<u_LightCount;++i){vec3 lp=texelFetch(u_Lights,i*27).xyz;vec4 ln=texelFetch(u_Lights,i*27+1);vec3 flux=texelFetch(u_Lights,i*27+2).rgb;
 vec3 delta=lp-p;float r2=dot(delta,delta);if(r2<=1e-16)continue;vec3 l=delta*inversesqrt(r2);float cn=dot(ln.xyz,-l);cn=ln.w>0?abs(cn):max(cn,0);float ns=max(dot(n,l),0);if(cn>0&&ns>0)result+=flux*brdf(n,v,l)*cn*ns/r2*visibility(i,p);}return result;}
vec3 emitted(){return gl_FrontFacing||u_TwoSided?u_Emission:vec3(0);}
// Search only current camera information. The alpha channel marks valid surfaces.
bool screenHit(vec3 origin,vec3 direction,sampler2D positions,int steps,out vec2 hit,out vec3 point){
 float lastT=0,lastDelta=-1e30;vec2 lastUv=vec2(0);for(int i=1;i<=512;++i){if(i>steps)break;
 float t=u_Range*float(i)/float(steps);vec3 p=origin+direction*t;vec4 clip=u_VP*vec4(p,1);if(clip.w<=0)break;vec3 ndc=clip.xyz/clip.w;vec2 at=ndc.xy*.5+.5;if(any(lessThan(at,vec2(0)))||any(greaterThan(at,vec2(1)))||ndc.z>1)break;
 vec4 surface=texture(positions,at);float delta=dot(p-surface.xyz,u_Forward);
 if(surface.w>0&&delta>=0&&lastDelta<0){float lo=lastT,hi=t;vec4 refined=surface;for(int j=0;j<6;++j){float mid=(lo+hi)*.5;vec3 a=origin+direction*mid;vec4 c=u_VP*vec4(a,1);hit=c.xy/c.w*.5+.5;refined=texture(positions,hit);if(refined.w>0&&dot(a-refined.xyz,u_Forward)>0)hi=mid;else lo=mid;}
 vec3 candidate=origin+direction*hi;if(refined.w>0&&abs(dot(candidate-refined.xyz,u_Forward))<=u_Thickness){point=refined.xyz;return true;}}
 lastT=t;lastDelta=surface.w>0?delta:-1e30;lastUv=at;}
 hit=lastUv;point=origin;return false;
}
vec3 reflection(vec3 p,vec3 n,vec3 f0){vec3 v=outgoing(p),d=reflect(-v,n);vec2 ab=texture(u_Brdf,vec2(max(dot(n,v),0),u_Roughness)).rg;vec3 weight=f0*ab.x+ab.y;
 vec3 fallback=textureLod(u_Probe,d,u_Roughness*u_ProbeMip).rgb*weight;vec2 at;vec3 point;
 if(screenHit(p+n*u_Thickness*.1,d,u_Positions,u_ReflectionSteps,at,point)){
 float edge=min(min(at.x,at.y),min(1-at.x,1-at.y));float confidence=smoothstep(0,.05,edge);vec3 color=textureLod(u_Source,at,u_Roughness*u_ProbeMip).rgb*weight;return mix(fallback,color,confidence);}
 return fallback;
}
float radical(uint x){x=(x<<16)|(x>>16);x=((x&0x55555555u)<<1)|((x&0xaaaaaaaau)>>1);x=((x&0x33333333u)<<2)|((x&0xccccccccu)>>2);x=((x&0x0f0f0f0fu)<<4)|((x&0xf0f0f0f0u)>>4);x=((x&0x00ff00ffu)<<8)|((x&0xff00ff00u)>>8);return float(x)*2.3283064365386963e-10;}
vec3 ggx(vec2 xi,vec3 n,float roughness){float a=max(.001,roughness*roughness),phi=2*PI*xi.x,c=sqrt((1-xi.y)/(1+(a*a-1)*xi.y)),s=sqrt(max(0,1-c*c));vec3 h=vec3(cos(phi)*s,sin(phi)*s,c);vec3 up=abs(n.z)<.999?vec3(0,0,1):vec3(1,0,0);vec3 t=normalize(cross(up,n));return normalize(t*h.x+cross(n,t)*h.y+n*h.z);}
