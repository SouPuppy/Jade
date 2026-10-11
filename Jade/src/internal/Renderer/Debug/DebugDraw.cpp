#include "DebugDraw.hpp"
#include <JadeShaders.hpp>
#include <array>
#include "../RaySceneAccess.hpp"
#include "../../Mesh/MeshAccess.hpp"
#include "../../../Renderer/Common/Math.hpp"
namespace Jade::Detail {
DebugDraw::DebugDraw(OpenGLDevice &device) : m_Device(device) {}
DebugDraw::~DebugDraw() = default;
void DebugDraw::lines(std::span<const Vec3> points,const Matrix4& transform,Vec3 color,bool depth) {
  m_Device.drawLines(points,transform,color,depth);
}
void DebugDraw::wireframe(const Renderer::SceneSnapshot &snapshot, Renderer::ImageSize size)
{
  if (!snapshot.camera)
    return;
  const auto projection = snapshot.camera->viewProjection(size);
  std::vector<Vec3> edges;
  for (const auto &instance : snapshot.instances) {
    const auto &mesh = instance.mesh->data();
    edges.clear();
    edges.reserve(mesh.indices.size() * 2);
    for (std::size_t index = 0; index < mesh.indices.size(); index += 3) {
      const Vec3 a = mesh.vertices[mesh.indices[index]].position,
                 b = mesh.vertices[mesh.indices[index + 1]].position,
                 c = mesh.vertices[mesh.indices[index + 2]].position;
      edges.insert(edges.end(), {a, b, b, c, c, a});
    }
    lines(edges, projection * instance.objectToWorld, {0.8f, 0.85f, 0.9f}, true);
  }
}
void DebugDraw::axes(const Renderer::CameraFrame &camera, Renderer::ImageSize size)
{
  const auto projection = camera.viewProjection(size);
  const std::array<Vec3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  for (const auto axis : axes) {
    const std::array<Vec3, 2> points{{{}, axis}};
    lines(points, projection, axis, false);
  }
}
void DebugDraw::paths(std::span<const Vec3> points,const Renderer::CameraFrame& camera,Renderer::ImageSize size,Vec3 color) {
  lines(points,camera.viewProjection(size),color,false);
}
void DebugDraw::frustum(const Renderer::CameraFrame& source,const Renderer::CameraFrame& observer,Renderer::ImageSize size) {
  std::array<Vec3,8> corners;
  const std::array<Vec2,4> pixels{{{0,0},{static_cast<float>(size.width),0},
    {static_cast<float>(size.width),static_cast<float>(size.height)},{0,static_cast<float>(size.height)}}};
  for(int i=0;i<4;++i) {
    const auto ray=source.generateRay(pixels[i],size);
    corners[i]=ray.at(ray.tMin);
    corners[i+4]=ray.at(std::min(ray.tMax,15.f));
  }
  std::vector<Vec3> edges;
  for(int i=0;i<4;++i) {
    const int next=(i+1)%4;
    edges.insert(edges.end(),{corners[i],corners[next],corners[i+4],corners[next+4],corners[i],corners[i+4]});
  }
  paths(edges,observer,size,{1,.8f,.2f});
}
void DebugDraw::scene(const Renderer::SceneSnapshot& snapshot,Renderer::ImageSize size,Renderer::DebugViewOptions options) {
  if(!snapshot.camera) return;
  const auto vp=snapshot.camera->viewProjection(size);
  if(options.entityAxes) {
    for(const auto& transform:snapshot.entityTransforms) {
      const auto origin=Renderer::Math::point(transform,{});
      const std::array<Vec3,3> axes{{{1,0,0},{0,1,0},{0,0,1}}};
      for(const auto axis:axes) {
        const auto end=origin+Renderer::Math::unit(Renderer::Math::vector(transform,axis))*.4f;
        const std::array<Vec3,2> points{origin,end};lines(points,vp,axis,false);
      }
    }
  }
  if(options.cameraFrustums) for(const auto& camera:snapshot.cameras) frustum(camera,*snapshot.camera,size);
  if(options.lights) {
    for(const auto& light:Renderer::realtimeLights(snapshot)) {
      const std::array<Vec3,2> points{light.position,light.position+light.direction*2};
      lines(points,vp,{1,1,0},false);
    }
  }
  if(!options.bvh || snapshot.instances.empty()) return;
  const auto rayScene=m_Compiler.compile(snapshot);
  std::vector<Vec3> edges;
  const auto add=[&](const Bounds& bounds,const Matrix4& model) {
    if(!bounds.valid() || edges.size()>24576) return;
    std::array<Vec3,8> vertices;
    for(int i=0;i<8;++i) vertices[i]=Renderer::Math::point(model,
      {i&1?bounds.high.x:bounds.low.x,i&2?bounds.high.y:bounds.low.y,i&4?bounds.high.z:bounds.low.z});
    for(int i=0;i<8;++i) for(int bit=1;bit<=4;bit*=2) if(!(i&bit)) edges.insert(edges.end(),{vertices[i],vertices[i|bit]});
  };
  for(const auto& node:RaySceneAccess::acceleration(rayScene).nodes()) add(node.bounds,Matrix4::Identity());
  lines(edges,vp,{.5f,.3f,1},false);edges.clear();
  for(const auto& instance:snapshot.instances)
    for(const auto& node:MeshAccess::acceleration(*instance.mesh).nodes()) add(node.bounds,instance.objectToWorld);
  lines(edges,vp,{.3f,.7f,1},false);
}
void DebugDraw::colliders(const CollisionDebugLines &debug, const Renderer::CameraFrame &camera,
                          Renderer::ImageSize size)
{
  const auto projection = camera.viewProjection(size);
  lines(debug.shapes, projection, {1, 0.5f, 0}, false);
  lines(debug.aabbs, projection, {0, 1, 1}, false);
  lines(debug.normals, projection, {0, 1, 0}, false);
  lines(debug.points, projection, {1, 0, 0}, false);
  lines(debug.fieldForces, projection, {0, 0.25f, 1}, false);
  lines(debug.totalForces, projection, {1, 1, 0}, false);
}
} // namespace Jade::Detail
