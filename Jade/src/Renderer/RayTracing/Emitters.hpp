#pragma once
#include <Jade/Renderer/Common/SceneSnapshot.hpp>
#include <stop_token>
#include <unordered_map>
namespace Jade::Detail {
using Renderer::LinearRGB;
struct Emitter {
  Vec3 a, b, c, normal;
  LinearRGB emission;
  std::uint32_t instance, primitive;
  float area;
  double cumulative = 0, weight = 0;
  bool twoSided = false;
  GeometryKind kind = GeometryKind::Mesh;
  Matrix4 model = Matrix4::Identity(), inverse = Matrix4::Identity();
};

struct EmitterDistribution {
  std::vector<Emitter> emitters;
  std::vector<std::unordered_map<std::uint32_t, std::size_t>> emitterIndices;
  double totalWeight = 0;
  Renderer::EmitterSample sample(float selection, Vec2 uv) const;
};
std::shared_ptr<const EmitterDistribution> compileEmitters(const Renderer::SceneSnapshot &,
                                                           std::stop_token = {});
bool sameEmitters(const Renderer::SceneSnapshot &, const Renderer::SceneSnapshot &);
} // namespace Jade::Detail
