#include "RasterScene.hpp"
#include "OpenGLProgram.hpp"
#include "../../../../Renderer/Common/Math.hpp"
#include "../../../Acceleration/BVH.hpp"
#include <JadeShaders.hpp>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <Jade/Renderer/RayTracing/RayScene.hpp>

namespace Jade::Detail {
namespace {
using namespace Renderer;
struct Float4 { float x,y,z,w; };
Float4 pack(Vec3 v,float w=0) { return {v.x,v.y,v.z,w}; }
struct Geometry {
  std::shared_ptr<const Mesh> owner;
  GLuint vao=0, vertices=0, indices=0;
  GLsizei count=0;
};
CameraFrame lightCamera(Vec3 position,Vec3 forward,Vec3 up) {
  CameraFrame camera;
  camera.position=position; camera.forward=Math::unit(forward);
  camera.right=Math::unit(cross(camera.forward,up)); camera.up=cross(camera.right,camera.forward);
  camera.lens.fovDegrees=90; camera.lens.nearPlane=.05f; camera.lens.farPlane=10000;
  return camera;
}
void uniform3(const OpenGLProgram& p,const char* name,Vec3 v) {
  glUniform3f(p.Uniform(name),v.x,v.y,v.z);
}
void uniform3(const OpenGLProgram& p,const char* name,LinearRGB v) { uniform3(p,name,Vec3{v.r,v.g,v.b}); }
void check() {
  if(const auto error=glGetError();error!=GL_NO_ERROR)
    throw std::runtime_error("Raster device failed: OpenGL error "+std::to_string(error));
}
}
struct RasterScene::Impl {
  OpenGLProgram raster{Shaders::RasterVertex,Shaders::RasterFragment};
  OpenGLProgram tone{Shaders::DisplayVertex,Shaders::ToneMapFragment};
  GLuint hdr=0,depth=0,fbo=0,shadow=0,shadowFbo=0,atlas=0,rayBuffer=0,rayTexture=0,quad=0;
  ImageSize size;
  int atlasWidth=1,atlasHeight=1;
  std::vector<std::shared_ptr<const Texture>> textures;
  std::unordered_map<const Texture*,int> layers;
  std::unordered_map<const Mesh*,Geometry> geometries;
  std::vector<RenderInstance> previous;
  std::array<Matrix4,48> matrices{};
  int triangleOffset=0;
  JobSystem* jobs=nullptr;
  GLuint cpuShadow=0;
  bool rayMode=false;
  RaySceneCompiler cpuCompiler;
  std::vector<RenderLight> lights;
  explicit Impl(JobSystem* inputJobs):jobs(inputJobs) {
    glGenTextures(1,&cpuShadow);
    glGenFramebuffers(1,&fbo); glGenFramebuffers(1,&shadowFbo);
    glGenTextures(1,&hdr); glGenRenderbuffers(1,&depth);
    glGenTextures(1,&shadow); glGenTextures(1,&atlas);
    glGenBuffers(1,&rayBuffer); glGenTextures(1,&rayTexture); glGenVertexArrays(1,&quad);
    glBindTexture(GL_TEXTURE_2D_ARRAY,shadow);
    glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_DEPTH_COMPONENT24,512,512,48,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER,shadowFbo);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    check();
  }
  ~Impl() {
    for(auto& [key,g]:geometries) release(g);
    glDeleteTextures(1,&cpuShadow);
    glDeleteTextures(1,&hdr); glDeleteRenderbuffers(1,&depth);
    glDeleteTextures(1,&shadow); glDeleteTextures(1,&atlas); glDeleteTextures(1,&rayTexture);
    glDeleteBuffers(1,&rayBuffer); glDeleteVertexArrays(1,&quad);
    glDeleteFramebuffers(1,&fbo); glDeleteFramebuffers(1,&shadowFbo);
  }
  static void release(Geometry& g) {
    glDeleteVertexArrays(1,&g.vao); glDeleteBuffers(1,&g.vertices); glDeleteBuffers(1,&g.indices);
  }
  Geometry& geometry(const std::shared_ptr<const Mesh>& mesh) {
    auto [it,inserted]=geometries.try_emplace(mesh.get()); auto& g=it->second;
    if(!inserted) return g;
    const auto& data=mesh->data();
    if(data.indices.size()>static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
      throw std::invalid_argument("Raster mesh exceeds draw index limit.");
    g.owner=mesh; g.count=static_cast<GLsizei>(data.indices.size());
    glGenVertexArrays(1,&g.vao); glGenBuffers(1,&g.vertices); glGenBuffers(1,&g.indices);
    glBindVertexArray(g.vao); glBindBuffer(GL_ARRAY_BUFFER,g.vertices);
    glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(data.vertices.size()*sizeof(Vertex)),data.vertices.data(),GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,normal)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,g.indices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,static_cast<GLsizeiptr>(data.indices.size()*sizeof(std::uint32_t)),data.indices.data(),GL_STATIC_DRAW);
    return g;
  }
  void resize(ImageSize next) {
    if(size==next) return;
    (void)next.pixels(); size=next;
    glBindTexture(GL_TEXTURE_2D,hdr);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,size.width,size.height,0,GL_RGBA,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glBindRenderbuffer(GL_RENDERBUFFER,depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,size.width,size.height);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,hdr,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("Incomplete raster target.");
  }
  void uploadTextures(const SceneSnapshot& scene) {
    std::vector<std::shared_ptr<const Texture>> next;
    for(const auto& instance:scene.instances)
      if(instance.texture && std::find(next.begin(),next.end(),instance.texture)==next.end()) next.push_back(instance.texture);
    if(next==textures && !layers.empty()) return;
    textures=next; layers.clear(); atlasWidth=atlasHeight=1;
    for(const auto& t:textures) { atlasWidth=std::max(atlasWidth,t->width()); atlasHeight=std::max(atlasHeight,t->height()); }
    GLint maximum=0,maximumLayers=0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum); glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS,&maximumLayers);
    if(atlasWidth>maximum || atlasHeight>maximum || textures.size()>static_cast<std::size_t>(maximumLayers))
      throw std::invalid_argument("Sprite atlas exceeds graphics device limits.");
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY,atlas);
    glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_SRGB8_ALPHA8,atlasWidth,atlasHeight,static_cast<GLsizei>(std::max<std::size_t>(1,textures.size())),0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    for(std::size_t i=0;i<textures.size();++i) {
      const auto& t=textures[i]; layers[t.get()]=static_cast<int>(i);
      glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,static_cast<GLint>(i),t->width(),t->height(),1,GL_RGBA,GL_UNSIGNED_BYTE,t->pixels().data());
    }
  }
  Vec2 texUv(const RenderInstance& instance,Vec3 p) {
    const auto& r=instance.region;
    return {std::clamp(r.x+(p.x+.5f)*r.width,r.x+.5f/instance.texture->width(),r.x+r.width-.5f/instance.texture->width())*instance.texture->width()/atlasWidth,
            std::clamp(r.y+(1-p.y)*r.height,r.y+.5f/instance.texture->height(),r.y+r.height-.5f/instance.texture->height())*instance.texture->height()/atlasHeight};
  }
  void uploadRays(const SceneSnapshot& scene) {
    if(scene.instances==previous) return;
    previous=scene.instances;
    std::vector<std::array<Float4,5>> triangles;
    std::vector<Bounds> bounds;
    for(std::size_t instanceIndex=0;instanceIndex<scene.instances.size();++instanceIndex) {
      const auto& instance=scene.instances[instanceIndex];
      const auto& data=instance.mesh->data();
      for(std::size_t i=0;i<data.indices.size();i+=3) {
        std::array<Vec3,3> local{},world{}; Bounds b;
        for(int j=0;j<3;++j) {
          local[j]=data.vertices[data.indices[i+j]].position;
          world[j]=Math::point(instance.objectToWorld,local[j]); b.add(world[j]);
        }
        const int layer=instance.texture?layers.at(instance.texture.get()):-1;
        std::array<Float4,5> tri{pack(world[0],static_cast<float>(layer)),pack(world[1],instance.alphaCutoff),pack(world[2],static_cast<float>(instanceIndex)),Float4{},Float4{}};
        if(instance.texture) {
          const auto a=texUv(instance,local[0]),bb=texUv(instance,local[1]),c=texUv(instance,local[2]);
          tri[3]={a.x,a.y,bb.x,bb.y}; tri[4]={c.x,c.y,0,0};
        }
        triangles.push_back(tri); bounds.push_back(b);
      }
    }
    BVH tree; tree.build(bounds);
    std::vector<Float4> packed;
    for(const auto& node:tree.nodes()) {
      // Conservative world bounds prevent grazing ray misses.
      packed.push_back(pack(node.bounds.low-Vec3{.0001f,.0001f,.0001f},static_cast<float>(node.left)));
      packed.push_back(pack(node.bounds.high+Vec3{.0001f,.0001f,.0001f},static_cast<float>(node.right)));
      packed.push_back({static_cast<float>(node.start),static_cast<float>(node.count),0,0});
    }
    triangleOffset=static_cast<int>(packed.size());
    for(const auto index:tree.order()) packed.insert(packed.end(),triangles[index].begin(),triangles[index].end());
    if(packed.empty()) packed={{0,0,0,0},{0,0,0,0},{0,1,0,0},{0,0,0,-1},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
    GLint maximum=0;glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&maximum);
    if(packed.size()>static_cast<std::size_t>(maximum)) throw std::invalid_argument("Hybrid scene exceeds buffer texture limits.");
    glBindBuffer(GL_TEXTURE_BUFFER,rayBuffer);
    glBufferData(GL_TEXTURE_BUFFER,static_cast<GLsizeiptr>(packed.size()*sizeof(Float4)),packed.data(),GL_DYNAMIC_DRAW);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_BUFFER,rayTexture);glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,rayBuffer);
  }
  void drawInstances(const SceneSnapshot& scene,const Matrix4& vp,bool shadowPass) {
    glUniformMatrix4fv(raster.Uniform("viewProjection"),1,GL_FALSE,vp.data());
    glUniform1i(raster.Uniform("depthOnly"),shadowPass?1:0);
    for(const auto& instance:scene.instances) {
      // Emissive geometry is the source, not a blocker for its own center approximation.
      if(shadowPass && instance.material.emission.maximum()>0) continue;
      auto& g=geometry(instance.mesh);
      glUniformMatrix4fv(raster.Uniform("model"),1,GL_FALSE,instance.objectToWorld.data());
      uniform3(raster,"baseColor",instance.material.baseColor); uniform3(raster,"emission",instance.material.emission);
      glUniform1i(raster.Uniform("unlit"),instance.unlit?1:0);
      glUniform1i(raster.Uniform("materialModel"),static_cast<int>(instance.material.model));
      glUniform1f(raster.Uniform("roughness"),instance.material.roughness);
      glUniform1i(raster.Uniform("textureLayer"),instance.texture?layers.at(instance.texture.get()):-1);
      glUniform1f(raster.Uniform("alphaCutoff"),instance.alphaCutoff);
      const auto& r=instance.region;
      const float sx=instance.texture?static_cast<float>(instance.texture->width())/atlasWidth:1;
      const float sy=instance.texture?static_cast<float>(instance.texture->height())/atlasHeight:1;
      glUniform4f(raster.Uniform("region"),r.x*sx,r.y*sy,r.width*sx,r.height*sy);
      glBindVertexArray(g.vao); glDrawElements(GL_TRIANGLES,g.count,GL_UNSIGNED_INT,nullptr);
    }
  }
  void prepare(const SceneSnapshot& scene,RenderTarget target,bool rayShadows) {
    if(!scene.camera) return;
    rayMode=rayShadows;
    resize(target.size); uploadTextures(scene);
    if(rayShadows) uploadRays(scene);
    std::unordered_set<const Mesh*> active;
    for(const auto& i:scene.instances) active.insert(i.mesh.get());
    for(auto it=geometries.begin();it!=geometries.end();) {
      if(!active.contains(it->first)) {release(it->second); it=geometries.erase(it);} else ++it;
    }
    lights=realtimeLights(scene);
    if(lights.size()>8) throw std::invalid_argument("Realtime pipeline supports at most eight lights.");
  }
  void shadows(const SceneSnapshot& scene,bool rayShadows) {
    if(!scene.camera) return;
    glUseProgram(raster.Handle());glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);
    glUniform1i(raster.Uniform("spriteAtlas"),0);glUniform1i(raster.Uniform("shadowMaps"),1);glUniform1i(raster.Uniform("rayData"),2);
    glUniform2f(raster.Uniform("atlasSize"),static_cast<float>(atlasWidth),static_cast<float>(atlasHeight));
    glUniform1i(raster.Uniform("rayShadows"),rayShadows?1:0);glUniform1i(raster.Uniform("triangleOffset"),triangleOffset);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_ARRAY,atlas);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D_ARRAY,shadow);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_BUFFER,rayTexture);
    std::array<Float4,8> positions{},directions{},intensities{};
    std::array<GLint,8> sourceInstances{};sourceInstances.fill(-1);
    const std::array<Vec3,6> faceDirections{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    const std::array<Vec3,6> faceUps{{{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}}};
    for(std::size_t i=0;i<lights.size();++i) {
      const auto& l=lights[i];sourceInstances[i]=l.sourceInstance;positions[i]=pack(l.position,static_cast<float>(l.type));directions[i]=pack(l.direction,l.twoSided?1.f:0.f);
      const float area=l.type==LightType::Area?l.size.x*l.size.y:1;
      intensities[i]={l.intensity.r*area,l.intensity.g*area,l.intensity.b*area,0};
      for(int face=0;face<6;++face) {
        auto camera=lightCamera(l.position,faceDirections[face],faceUps[face]);
        if(l.type==LightType::Directional) {
          const Vec3 up=std::abs(l.direction.y)>.95f?Vec3{0,0,1}:Vec3{0,1,0};
          camera=lightCamera(scene.camera->position-l.direction*100,l.direction,up);
          camera.lens.projection=Projection::Orthographic;camera.lens.viewHeight=100;camera.lens.farPlane=500;
        }
        const auto index=i*6+face;matrices[index]=camera.viewProjection({512,512});
        if(!rayShadows && (l.type!=LightType::Directional || face==0)) {
          glBindFramebuffer(GL_FRAMEBUFFER,shadowFbo);
          glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,shadow,0,static_cast<GLint>(index));
          glViewport(0,0,512,512);glClear(GL_DEPTH_BUFFER_BIT);
          drawInstances(scene,matrices[index],true);
        }
      }
    }
    glUniform1iv(raster.Uniform("lightInstance[0]"),8,sourceInstances.data());
    glUniform1i(raster.Uniform("lightCount"),static_cast<int>(lights.size()));
    glUniform4fv(raster.Uniform("lightPosition[0]"),8,reinterpret_cast<float*>(positions.data()));
    glUniform4fv(raster.Uniform("lightDirection[0]"),8,reinterpret_cast<float*>(directions.data()));
    glUniform4fv(raster.Uniform("lightIntensity[0]"),8,reinterpret_cast<float*>(intensities.data()));
    glUniformMatrix4fv(raster.Uniform("lightMatrix[0]"),48,GL_FALSE,matrices[0].data());
  }
  void cpuShadows(const SceneSnapshot& scene,PipelineSettings settings) {
    if(lights.empty()) return;
    if(!std::isfinite(settings.shadowScale) || settings.shadowScale<=0 || settings.shadowScale>1)
      throw std::invalid_argument("Shadow scale must be within (0,1].");
    const ImageSize shadowSize{std::max(1,static_cast<int>(std::lround(size.width*settings.shadowScale))),
      std::max(1,static_cast<int>(std::lround(size.height*settings.shadowScale)))};
    const auto rayScene=cpuCompiler.compile(scene);
    std::vector<unsigned char> masks(shadowSize.pixels()*lights.size(),255);
    const auto row=[&](std::size_t y) {
      for(int x=0;x<shadowSize.width;++x) {
        SurfaceHit hit;
        // Camera samples use top-left coordinates; mask storage follows OpenGL bottom-left rows.
        if(!rayScene.intersect(scene.camera->generateRay({x+.5f,static_cast<float>(shadowSize.height)-static_cast<float>(y)-.5f},shadowSize),hit) || rayScene.unlit(hit)) continue;
        for(std::size_t i=0;i<lights.size();++i) {
          const auto& l=lights[i];
          const auto origin=hit.position+hit.shadingNormal*.002f;
          const auto delta=l.position-origin;
          const float distance=l.type==LightType::Directional?10000.f:Math::length(delta);
          if(distance<=.002f) continue;
          const auto direction=l.type==LightType::Directional?-l.direction:delta*(1/distance);
          Ray ray{origin,direction,0,distance-.002f};
          if(rayScene.occluded(ray,l.sourceInstance<0?UINT32_MAX:static_cast<std::uint32_t>(l.sourceInstance))) masks[i*shadowSize.pixels()+y*shadowSize.width+x]=0;
        }
      }
    };
    if(jobs) {const auto task=jobs->parallelFor(static_cast<std::size_t>(shadowSize.height),row,{JobCategory::RayTracing});jobs->wait(task,WaitMode::Help);}
    else for(std::size_t y=0;y<static_cast<std::size_t>(shadowSize.height);++y) row(y);
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D_ARRAY,cpuShadow);
    glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_R8,shadowSize.width,shadowSize.height,static_cast<GLsizei>(lights.size()),0,GL_RED,GL_UNSIGNED_BYTE,nullptr);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,0,shadowSize.width,shadowSize.height,static_cast<GLsizei>(lights.size()),GL_RED,GL_UNSIGNED_BYTE,masks.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  }
  void drawGeometry(const SceneSnapshot& scene,PipelineSettings settings) {
    if(!scene.camera) return;
    if(rayMode && settings.cpuRayShadows && !settings.normals) cpuShadows(scene,settings);
    glUseProgram(raster.Handle());
    // Geometry diagnostics can draw without executing the shadow pass first.
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);
    glUniform1i(raster.Uniform("spriteAtlas"),0);glUniform1i(raster.Uniform("shadowMaps"),1);
    glUniform1i(raster.Uniform("rayData"),2);glUniform1i(raster.Uniform("rayShadows"),rayMode?1:0);
    glUniform2f(raster.Uniform("atlasSize"),static_cast<float>(atlasWidth),static_cast<float>(atlasHeight));
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_ARRAY,atlas);
    glUniform1i(raster.Uniform("cpuRayShadows"),rayMode && settings.cpuRayShadows?1:0);
    glUniform1i(raster.Uniform("cpuShadowMaps"),3);glUniform2f(raster.Uniform("targetSize"),static_cast<float>(size.width),static_cast<float>(size.height));
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,size.width,size.height);
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glUniform1i(raster.Uniform("normalOutput"),settings.normals?1:0);uniform3(raster,"cameraPosition",scene.camera->position);
    drawInstances(scene,scene.camera->viewProjection(size),false);
  }
  void composite(PipelineSettings settings) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    glBlitFramebuffer(0,0,size.width,size.height,0,0,size.width,size.height,GL_DEPTH_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glUseProgram(tone.Handle());glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,hdr);
    glUniform1i(tone.Uniform("hdr"),0);glUniform1f(tone.Uniform("exposureEV"),settings.exposureEV);
    glUniform1i(tone.Uniform("normalOutput"),settings.normals?1:0);
    glBindVertexArray(quad);glDrawArrays(GL_TRIANGLES,0,3);glDepthMask(GL_TRUE);
    check();
  }
};
RasterScene::RasterScene(JobSystem* jobs):m_Impl(std::make_unique<Impl>(jobs)) {}
RasterScene::~RasterScene()=default;
void RasterScene::prepare(const Renderer::SceneSnapshot& s,Renderer::RenderTarget t,bool rayShadows) {m_Impl->prepare(s,t,rayShadows);}
void RasterScene::shadows(const Renderer::SceneSnapshot& s,bool rayShadows) {m_Impl->shadows(s,rayShadows);}
void RasterScene::geometry(const Renderer::SceneSnapshot& s,Renderer::PipelineSettings p) {m_Impl->drawGeometry(s,p);}
void RasterScene::composite(Renderer::PipelineSettings p) {m_Impl->composite(p);}
void RasterScene::draw(const Renderer::SceneSnapshot& s,Renderer::RenderTarget t,Renderer::PipelineSettings p,bool rayShadows) {
  if(!s.camera) return;prepare(s,t,rayShadows);shadows(s,rayShadows);geometry(s,p);composite(p);
}
} // namespace Jade::Detail
