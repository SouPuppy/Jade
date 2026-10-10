#include <Jade/Jade.hpp>
#include "internal/Acceleration/BVH.hpp"
#include "internal/Mesh/MeshAccess.hpp"
#include "internal/Renderer/RaySceneAccess.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <numeric>
#include <functional>
namespace {
using namespace Jade;
using namespace Jade::Renderer;
using namespace Jade::Detail;

SceneSnapshot instances(std::size_t count)
{
  SceneSnapshot snapshot;
  auto mesh = std::make_shared<const Mesh>(Mesh::compile(MeshData::cube(0.2f)));
  for (std::size_t i = 0; i < count; ++i) {
    Matrix4 model = Matrix4::Identity();
    model.at(0, 3) = static_cast<float>(i) * 2;
    snapshot.instances.push_back({mesh, model, Materials::Diffuse(), i + 1});
  }
  return snapshot;
}
struct MedianReference {
  struct Node {
    Bounds bounds;
    std::vector<std::uint32_t> ids;
    int left = -1, right = -1;
  };
  std::vector<Node> nodes;
  std::vector<Bounds> boxes;
  int make(std::vector<std::uint32_t> ids)
  {
    const int index = static_cast<int>(nodes.size());
    nodes.emplace_back();
    Bounds box, centroids;
    for (auto id : ids) {
      box.add(boxes[id]);
      centroids.add(boxes[id].center());
    }
    nodes[index].bounds = box;
    if (ids.size() <= 4)
      nodes[index].ids = std::move(ids);
    else {
      auto extent = centroids.high - centroids.low;
      int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : extent.y >= extent.z ? 1 : 2;
      std::sort(ids.begin(), ids.end(), [&](auto a, auto b) {
        const float ca = Coordinate(boxes[a].center(), axis), cb = Coordinate(boxes[b].center(), axis);
        return ca == cb ? a < b : ca < cb;
      });
      auto middle = ids.begin() + static_cast<std::ptrdiff_t>(ids.size() / 2);
      std::vector<std::uint32_t> left(ids.begin(), middle), right(middle, ids.end());
      const int l = make(std::move(left)), r = make(std::move(right));
      nodes[index].left = l;
      nodes[index].right = r;
    }
    return index;
  }
  explicit MedianReference(std::vector<Bounds> b) : boxes(std::move(b))
  {
    std::vector<std::uint32_t> ids(boxes.size());
    std::iota(ids.begin(), ids.end(), 0);
    make(std::move(ids));
  }
  double cost(int i = 0) const
  {
    const auto &n = nodes[i];
    if (!n.ids.empty())
      return static_cast<double>(n.ids.size());
    return 1 + (nodes[n.left].bounds.area() * cost(n.left) + nodes[n.right].bounds.area() * cost(n.right)) /
                   n.bounds.area();
  }
  TraversalStats candidates(const Ray &ray) const
  {
    TraversalStats result;
    std::function<void(int)> walk = [&](int i) {
      ++result.boundsTests;
      double entry;
      const auto &n = nodes[i];
      if (!n.bounds.hit(ray, ray.tMax, entry))
        return;
      if (!n.ids.empty())
        result.primitiveTests += n.ids.size();
      else {
        walk(n.left);
        walk(n.right);
      }
    };
    walk(0);
    return result;
  }
};
} // namespace

TEST(MaterialPresets, IndependentValuesAndEntityComposition)
{
  auto gold = Materials::Gold();
  gold.roughness = .8f;
  EXPECT_FLOAT_EQ(Materials::Gold().roughness, .18f);
  EXPECT_EQ(gold.model, MaterialModel::Metal);
  const auto glass = Materials::Glass();
  EXPECT_EQ(glass.model, MaterialModel::Dielectric);
  EXPECT_FLOAT_EQ(glass.ior, 1.5f);
  EXPECT_FLOAT_EQ(glass.roughness, 0);
  const auto light = Materials::Emissive({14, 12, 10}, true);
  EXPECT_EQ(light.baseColor, (LinearRGB{}));
  EXPECT_TRUE(light.twoSidedEmission);
  for (const auto material : {Materials::Diffuse(), Materials::Metal({.5f, .6f, .7f}), Materials::Gold(),
                              Materials::Silver(), Materials::Copper(), glass, light})
    EXPECT_NO_THROW(validateMaterial(material));
  Scene scene;
  Assets assets;
  auto bunny = scene.create();
  bunny.add<Geometry>(assets.add(MeshData::cube()));
  bunny.add<Material>(Materials::Gold());
  auto lamp = scene.create();
  lamp.add<Geometry>(bunny.get<Geometry>().mesh);
  lamp.add<Material>(Materials::Emissive());
  EXPECT_EQ(SceneSnapshot::capture(scene, assets).instances.size(), 2u);
}

TEST(CompiledMesh, AssetsPublishCompiledBlasAndCopiesRetainIt)
{
  Assets assets;
  auto source = MeshData::sphere();
  const auto handle = assets.add(source);
  const auto &mesh = assets.get(handle);
  const auto stats = MeshAccess::acceleration(mesh).statistics();
  EXPECT_EQ(stats.primitives, source.indices.size() / 3);
  EXPECT_GT(stats.nodes, 1u);
  const Mesh copy = mesh;
  EXPECT_EQ(MeshAccess::identity(copy), MeshAccess::identity(mesh));
  EXPECT_EQ(&copy.data(), &mesh.data());
  source.vertices.clear();
  EXPECT_EQ(mesh.data().vertices.size(), 482u);
  const auto imported = assets.load<Mesh>(std::filesystem::path(JADE_TEST_ASSET_DIR) / "bunny.obj");
  EXPECT_GT(MeshAccess::acceleration(assets.get(imported)).statistics().primitives, 900u);
  EXPECT_EQ(assets.load<Mesh>(std::filesystem::path(JADE_TEST_ASSET_DIR) / "bunny.obj"), imported);
}

TEST(CompiledMesh, StandaloneBlasPreservesOriginalTriangleIdsAndLifetime)
{
  MeshData data = MeshData::triangle({-1, -1, 0}, {1, -1, 0}, {0, 1, 0});
  data.indices.insert(data.indices.begin(), {0, 0, 0}); // Degenerate original primitive zero.
  const auto compiled = Mesh::compile(data);
  EXPECT_EQ(compiled.data().indices, data.indices);
  SurfaceHit hit;
  ASSERT_TRUE(MeshAccess::intersect(compiled, {{0, 0, 1}, {0, 0, -1}}, hit));
  EXPECT_EQ(hit.primitive, 1u);
  EXPECT_TRUE(MeshAccess::occluded(compiled, {{0, 0, 1}, {0, 0, -1}}));
  SceneSnapshot retained;
  const void *identity = nullptr;
  {
    Assets assets;
    auto handle = assets.add<Mesh>(compiled);
    identity = MeshAccess::identity(assets.get(handle));
    retained.instances.push_back({assets.share(handle), Matrix4::Identity(), Materials::Gold(), 19});
  }
  RaySceneCompiler first, second;
  const auto a = first.compile(retained), b = second.compile(retained);
  EXPECT_EQ(MeshAccess::identity(*retained.instances[0].mesh), identity);
  ASSERT_TRUE(a.intersect({{0, 0, 1}, {0, 0, -1}}, hit));
  EXPECT_TRUE(b.occluded({{0, 0, 1}, {0, 0, -1}}));
  EXPECT_EQ(hit.entity, 19u);
}

TEST(SceneAcceleration, ReusesRefitsAndKeepsPublishedScenesImmutable)
{
  auto input = instances(12);
  RaySceneCompiler compiler;
  const auto first = compiler.compile(input);
  EXPECT_EQ(RaySceneAccess::update(first), SceneAccelerationUpdate::Rebuilt);
  input.camera = CameraFrame{};
  input.instances[0].material = Materials::Gold();
  const auto material = compiler.compile(input);
  EXPECT_EQ(RaySceneAccess::update(material), SceneAccelerationUpdate::Reused);
  EXPECT_EQ(RaySceneAccess::geometryIdentity(first), RaySceneAccess::geometryIdentity(material));
  input.instances[0].objectToWorld.at(1, 3) = .03f;
  const auto moved = compiler.compile(input);
  EXPECT_EQ(RaySceneAccess::update(moved), SceneAccelerationUpdate::Refit);
  EXPECT_NE(RaySceneAccess::geometryIdentity(first), RaySceneAccess::geometryIdentity(moved));
  SurfaceHit oldHit, newHit;
  ASSERT_TRUE(first.intersect({{0, 0, 1}, {0, 0, -1}}, oldHit));
  ASSERT_TRUE(moved.intersect({{0, 0, 1}, {0, 0, -1}}, newHit));
  EXPECT_EQ(oldHit.entity, newHit.entity);
  EXPECT_EQ(first.material(oldHit).model, MaterialModel::Diffuse);
  EXPECT_EQ(moved.material(newHit).model, MaterialModel::Metal);
  input.instances[0].objectToWorld.at(1, 3) = 5;
  const auto away = compiler.compile(input);
  EXPECT_FALSE(away.occluded({{0, 0, 1}, {0, 0, -1}}));
  EXPECT_TRUE(first.occluded({{0, 0, 1}, {0, 0, -1}}));
  std::swap(input.instances[0], input.instances[1]);
  EXPECT_EQ(RaySceneAccess::update(compiler.compile(input)), SceneAccelerationUpdate::Rebuilt);
  input.instances[0].mesh = std::make_shared<const Mesh>(Mesh::compile(MeshData::sphere()));
  EXPECT_EQ(RaySceneAccess::update(compiler.compile(input)), SceneAccelerationUpdate::Rebuilt);
  input.instances.pop_back();
  EXPECT_EQ(RaySceneAccess::update(compiler.compile(input)), SceneAccelerationUpdate::Rebuilt);
}

TEST(SceneAcceleration, QualityDegradationTriggersSahRebuild)
{
  auto input = instances(64);
  RaySceneCompiler compiler;
  const auto original = compiler.compile(input);
  // Interleave distant groups in the old contiguous leaves.
  for (std::size_t i = 0; i < input.instances.size(); ++i)
    input.instances[i].objectToWorld.at(0, 3) = static_cast<float>((i % 2) * 32 + i / 2) * 2;
  const auto rebuilt = compiler.compile(input);
  EXPECT_EQ(RaySceneAccess::update(rebuilt), SceneAccelerationUpdate::Rebuilt);
  EXPECT_GT(RaySceneAccess::statistics(rebuilt).nodes, 1u);
  EXPECT_NE(RaySceneAccess::geometryIdentity(original), RaySceneAccess::geometryIdentity(rebuilt));
  SurfaceHit hit;
  EXPECT_TRUE(rebuilt.intersect({{64, 0, 1}, {0, 0, -1}}, hit));
}

TEST(SceneAcceleration, EmitterDistributionTracksOnlyItsDependencies)
{
  auto input = instances(3);
  input.instances[2].material = Materials::Emissive();
  RaySceneCompiler compiler;
  const auto first = compiler.compile(input);
  input.camera = CameraFrame{};
  input.instances[0].material = Materials::Gold();
  input.instances[0].objectToWorld.at(1, 3) = .02f;
  const auto ordinary = compiler.compile(input);
  EXPECT_EQ(RaySceneAccess::emitterIdentity(first), RaySceneAccess::emitterIdentity(ordinary));
  input.instances[2].objectToWorld.at(1, 3) = 2;
  const auto moved = compiler.compile(input);
  EXPECT_NE(RaySceneAccess::emitterIdentity(first), RaySceneAccess::emitterIdentity(moved));
  EXPECT_FLOAT_EQ(first.sampleEmitter(.5f, {.4f, .3f}).position.y + 2,
                  moved.sampleEmitter(.5f, {.4f, .3f}).position.y);
  input.instances[2].material.emission = {20, 10, 10};
  const auto changed = compiler.compile(input);
  EXPECT_NE(RaySceneAccess::emitterIdentity(moved), RaySceneAccess::emitterIdentity(changed));
  input.instances[2].material.twoSidedEmission = true;
  EXPECT_TRUE(compiler.compile(input).sampleEmitter(.5f, {.4f, .3f}).twoSided);
}

TEST(SceneAcceleration, CancellationAndFailureDoNotCommitCompilerCaches)
{
  auto input = instances(16);
  RaySceneCompiler compiler;
  const auto first = compiler.compile(input);
  auto moved = input;
  moved.instances[0].objectToWorld.at(1, 3) = 3;
  std::stop_source stop;
  stop.request_stop();
  EXPECT_EQ(compiler.compile(moved, stop.get_token()).instanceCount(), 0u);
  moved.instances[0].material.roughness = -1;
  EXPECT_THROW(compiler.compile(moved), std::invalid_argument);
  const auto restored = compiler.compile(input);
  EXPECT_EQ(RaySceneAccess::geometryIdentity(restored), RaySceneAccess::geometryIdentity(first));
  EXPECT_EQ(RaySceneAccess::emitterIdentity(restored), RaySceneAccess::emitterIdentity(first));
  EXPECT_EQ(RaySceneAccess::update(restored), SceneAccelerationUpdate::Reused);
}

TEST(SahBvh, NonuniformFixtureReducesCostAndCandidateTests)
{
  std::vector<Bounds> bounds;
  for (int i = 0; i < 48; ++i)
    bounds.push_back({{i * .01f, 0, 0}, {i * .01f + .005f, .01f, .01f}});
  bounds.push_back({{100, 0, 0}, {101, 1, 1}});
  const auto start = std::chrono::steady_clock::now();
  BVH sah;
  sah.build(bounds);
  const auto buildUs =
      std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
  MedianReference median(bounds);
  const Ray ray{{100.5f, .5f, 2}, {0, 0, -1}};
  float maximum = ray.tMax;
  TraversalStats stats;
  sah.visit(ray, maximum, false, [](auto) { return false; }, &stats);
  const auto reference = median.candidates(ray);
  EXPECT_LT(stats.primitiveTests, reference.primitiveTests);
  EXPECT_LE(stats.boundsTests, reference.boundsTests);
  EXPECT_LT(sah.statistics().cost, median.cost());
  RecordProperty("sah_build_us", std::to_string(buildUs));
  RecordProperty("sah_cost", std::to_string(sah.statistics().cost));
  RecordProperty("median_cost", std::to_string(median.cost()));
  RecordProperty("sah_primitive_tests", static_cast<int>(stats.primitiveTests));
  RecordProperty("median_primitive_tests", static_cast<int>(reference.primitiveTests));
}

TEST(SahBvh, NearFirstTraversalPrunesFarNodesAndShadowQueriesStop)
{
  BVH bvh;
  bvh.build({{{-.1f, -.1f, -.1f}, {.1f, .1f, .1f}}, {{99.9f, -.1f, -.1f}, {100.1f, .1f, .1f}}});
  const Ray ray{{105, 0, 0}, {-1, 0, 0}};
  float maximum = ray.tMax;
  std::vector<std::uint32_t> visited;
  EXPECT_TRUE(bvh.visit(ray, maximum, false, [&](auto id) {
    visited.push_back(id);
    maximum = 4.9f;
    return true;
  }));
  ASSERT_EQ(visited.size(), 1u);
  EXPECT_EQ(visited.front(), 1u);
  visited.clear();
  maximum = ray.tMax;
  EXPECT_TRUE(bvh.visit(ray, maximum, true, [&](auto id) {
    visited.push_back(id);
    return true;
  }));
  EXPECT_EQ(visited.size(), 1u);
}

TEST(SahBvh, DegenerateCentroidsDeepTreesExtremeAreasAndTransactionalCancellation)
{
  std::vector<Bounds> identical(128, {{-1, -1, -1}, {1, 1, 1}});
  BVH bvh;
  bvh.build(identical);
  EXPECT_EQ(bvh.statistics().primitives, 128u);
  EXPECT_LE(bvh.statistics().depth, 48u);
  std::vector<Bounds> exponential;
  for (int i = 0; i < 241; ++i) {
    const float x = std::ldexp(1.0f, i - 120);
    exponential.push_back({{x, 0, 0}, {x, .1f, .1f}});
  }
  bvh.build(exponential);
  EXPECT_EQ(bvh.statistics().depth, 48u);
  for (const auto &box : exponential) {
    const Ray ray{{box.low.x, .05f, 1}, {0, 0, -1}};
    float maximum = ray.tMax;
    EXPECT_TRUE(bvh.visit(ray, maximum, true, [&](auto id) {
      double entry;
      return exponential[id].hit(ray, maximum, entry);
    }));
  }
  const auto before = bvh.statistics();
  std::stop_source stop;
  stop.request_stop();
  EXPECT_THROW(bvh.build(identical, stop.get_token()), BuildCancelled);
  EXPECT_THROW(bvh.refit(exponential, stop.get_token()), BuildCancelled);
  EXPECT_EQ(bvh.statistics().nodes, before.nodes);
  EXPECT_EQ(bvh.statistics().cost, before.cost);
  Bounds extreme{{-1e38f, -1e38f, -1e38f}, {1e38f, 1e38f, 1e38f}};
  EXPECT_TRUE(std::isfinite(extreme.area()));
  bvh.build({extreme, {{0, 0, 0}, {1, 1, 1}}});
  EXPECT_TRUE(std::isfinite(bvh.statistics().cost));
}
