#version 330 core
in vec3 worldPosition, worldNormal;
in vec2 uv;
out vec4 result;
uniform sampler2DArray spriteAtlas, shadowMaps, cpuShadowMaps;
uniform int cpuRayShadows;
uniform vec2 targetSize;
uniform samplerBuffer rayData;
uniform vec4 region;
uniform vec2 atlasSize;
uniform vec3 baseColor, emission, cameraPosition;
uniform int textureLayer, unlit, depthOnly, normalOutput, rayShadows, lightCount, triangleOffset;
uniform float alphaCutoff, roughness;
uniform int materialModel;
uniform vec4 lightPosition[8], lightDirection[8], lightIntensity[8];
uniform mat4 lightMatrix[48];
uniform int lightInstance[8];
vec4 sprite(vec2 texUv, int layer) {
  return texture(spriteAtlas,vec3(texUv,float(layer)));
}
bool boundsHit(vec3 low, vec3 high, vec3 origin, vec3 direction, float maximum) {
  float entry=0.0, end=maximum;
  for (int axis=0;axis<3;++axis) {
    if (abs(direction[axis])<1e-10) {
      if(origin[axis]<low[axis] || origin[axis]>high[axis]) return false;
    } else {
      float a=(low[axis]-origin[axis])/direction[axis], b=(high[axis]-origin[axis])/direction[axis];
      entry=max(entry,min(a,b)); end=min(end,max(a,b));
      if(end<entry) return false;
    }
  }
  return true;
}
bool rayBlocked(vec3 origin,vec3 direction,float maximum,int ignoredInstance) {
  int stack[64]; int pending=1; stack[0]=0;
  while(pending>0) {
    int node=stack[--pending];
    vec4 low=texelFetch(rayData,node*3),high=texelFetch(rayData,node*3+1),links=texelFetch(rayData,node*3+2);
    if(!boundsHit(low.xyz,high.xyz,origin,direction,maximum)) continue;
    int count=int(links.y);
    if(count==0) { stack[pending++]=int(low.w); stack[pending++]=int(high.w); continue; }
    for(int i=0;i<count;++i) {
      int index=triangleOffset+(int(links.x)+i)*5;
      vec4 a=texelFetch(rayData,index),b=texelFetch(rayData,index+1),c=texelFetch(rayData,index+2);
      if(a.w < -1.5) {
        if(int(a.y)==ignoredInstance)continue;
        mat4 inverseModel=mat4(b,c,texelFetch(rayData,index+3),texelFetch(rayData,index+4));
        vec3 o=(inverseModel*vec4(origin,1)).xyz,d=(inverseModel*vec4(direction,0)).xyz;
        float t;
        if(int(a.x)==1) {
          float aa=dot(d,d),bb=dot(o,d),cc=dot(o,o)-1.0,disc=bb*bb-aa*cc;
          if(aa==0.0||disc<0.0)continue;
          float q=-bb-(bb>=0.0?1.0:-1.0)*sqrt(disc);
          float nearValue=q==0.0?-bb/aa:q/aa,farValue=q==0.0?nearValue:cc/q;
          if(nearValue>farValue){float swapValue=nearValue;nearValue=farValue;farValue=swapValue;}
          t=nearValue>.0001?nearValue:farValue;
        }else{
          if(d.y==0.0)continue;t=-o.y/d.y;vec3 p=o+t*d;
          if(abs(p.x)>.5||abs(p.z)>.5)continue;
        }
        if(t>.0001&&t<maximum)return true;continue;
      }
      if(int(c.w)==ignoredInstance) continue;
      vec3 edge1=b.xyz-a.xyz,edge2=c.xyz-a.xyz,p=cross(direction,edge2);
      float determinant=dot(edge1,p);
      if(abs(determinant)<1e-9) continue;
      vec3 s=origin-a.xyz; float u=dot(s,p)/determinant;
      if(u<0.0 || u>1.0) continue;
      vec3 q=cross(s,edge1); float v=dot(direction,q)/determinant;
      if(v<0.0 || u+v>1.0) continue;
      float t=dot(edge2,q)/determinant;
      if(t<=0.0001 || t>=maximum) continue;
      if(a.w>=0.0) {
        vec4 ab=texelFetch(rayData,index+3),cc=texelFetch(rayData,index+4);
        vec2 texUv=ab.xy*(1.0-u-v)+ab.zw*u+cc.xy*v;
        if(sprite(texUv,int(a.w)).a<b.w) continue;
      }
      return true;
    }
  }
  return false;
}
int cubeFace(vec3 d) {
  vec3 a=abs(d);
  if(a.x>=a.y && a.x>=a.z) return d.x>0.0?0:1;
  if(a.y>=a.z) return d.y>0.0?2:3;
  return d.z>0.0?4:5;
}
float visibility(int light,vec3 origin,vec3 direction,float distance) {
  if(cpuRayShadows!=0) return texture(cpuShadowMaps,vec3(gl_FragCoord.xy/targetSize,float(light))).r;
  // The endpoint must be measured from the biased origin; otherwise an area
  // emitter's own plane can appear just before the nominal endpoint.
  if(lightPosition[light].w!=0.0) {
    vec3 delta=lightPosition[light].xyz-origin;
    distance=length(delta); direction=delta/max(distance,1e-6);
  }
  if(rayShadows!=0) return rayBlocked(origin,direction,distance-.002,lightInstance[light])?0.0:1.0;
  int face=lightPosition[light].w==0.0?0:cubeFace(origin-lightPosition[light].xyz);
  int index=light*6+face;
  vec4 p=lightMatrix[index]*vec4(origin,1); vec3 coords=p.xyz/p.w*.5+.5;
  if(any(lessThan(coords,vec3(0))) || any(greaterThan(coords,vec3(1)))) return 1.0;
  float stored=texture(shadowMaps,vec3(coords.xy,float(index))).r;
  return coords.z-.0004<=stored?1.0:0.0;
}
void main() {
  vec3 color=baseColor;
  if(textureLayer>=0) {
    vec2 texUv=clamp(region.xy+uv*region.zw,region.xy+.5/atlasSize,region.xy+region.zw-.5/atlasSize);
    vec4 tex=sprite(texUv,textureLayer);
    if(tex.a<alphaCutoff) discard;
    color=tex.rgb;
  }
  if(depthOnly!=0) {result=vec4(0); return;}
  vec3 n=normalize(worldNormal); if(!gl_FrontFacing) n=-n;
  if(normalOutput!=0) {result=vec4(n*.5+.5,1); return;}
  if(unlit!=0) {result=vec4(color,1); return;}
  vec3 radiance=emission;
  // Small ambient term is a realtime approximation, not traced indirect light.
  radiance+=color*.025;
  for(int i=0;i<lightCount;++i) {
    vec3 delta=lightPosition[i].xyz-worldPosition;
    float distance=lightPosition[i].w==0.0?10000.0:length(delta);
    vec3 direction=lightPosition[i].w==0.0?-lightDirection[i].xyz:delta/max(distance,1e-6);
    vec3 irradiance=lightIntensity[i].rgb;
    if(lightPosition[i].w!=0.0) irradiance/=max(distance*distance,.01);
    if(lightPosition[i].w==2.0) {
      float facing=dot(lightDirection[i].xyz,-direction);
      irradiance*=lightDirection[i].w!=0.0?abs(facing):max(0.0,facing);
    }
    float cosine=max(dot(n,direction),0.0);
    if(cosine<=0.0) continue;
    float visible=visibility(i,worldPosition+n*.002,direction,distance);
    vec3 brdf=color/3.14159265;
    if(materialModel==1) {
      vec3 h=normalize(direction+normalize(cameraPosition-worldPosition));
      float exponent=mix(256.0,2.0,roughness);
      brdf=color*(exponent+2.0)/(2.0*3.14159265)*pow(max(dot(n,h),0.0),exponent);
    }
    result=vec4(0);
    radiance+=irradiance*brdf*cosine*visible;
  }
  result=vec4(radiance,1);
}
