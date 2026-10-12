#include <Jade/Jade.hpp>

#include "internal/Platform/Window.hpp"
#include "internal/Renderer/Host/ProgressiveSession.hpp"
#include "internal/Renderer/Presentation/OpenGLDisplay.hpp"
#include "internal/Renderer/Debug/DebugDraw.hpp"
#include "Renderer/Common/Math.hpp"

#include <gtest/gtest.h>
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <latch>
#include <semaphore>
#include <thread>

namespace {
using namespace Jade;
using namespace Jade::Renderer;
using Jade::Renderer::Math::unit;

struct Fixture {
  Scene scene{std::make_shared<World>()};
  Assets assets;
  Entity camera = scene.world().create();
  Fixture()
  {
    camera.add<Transform>().position = {0, 0, 5};
    camera.add<Camera>();
    scene.setCamera(camera);
  }
  Entity add(MeshData mesh, Material material = {}, Vec3 position = {}, Vec3 scale = {1, 1, 1})
  {
    const auto entity = scene.world().create();
    auto &pose = entity.add<Transform>();
    pose.position = position;
    pose.scale = scale;
    entity.add<Geometry>(assets.add(std::move(mesh)));
    entity.add<Material>(material);
    return entity;
  }
  SceneSnapshot snapshot() { return SceneSnapshot::capture(scene, assets); }
};

SurfaceHit flatHit(bool front = true)
{
  SurfaceHit hit;
  hit.geometricNormal = {0, 0, 1};
  hit.shadingNormal = hit.geometricNormal;
  hit.frontFace = front;
  return hit;
}

MeshData quad(float half, float z, bool upward = true, int divisions = 1)
{
  MeshData mesh;
  for (int y = 0; y < divisions; ++y)
    for (int x = 0; x < divisions; ++x) {
      const float left = -half + 2 * half * x / divisions, right = -half + 2 * half * (x + 1) / divisions;
      const float bottom = -half + 2 * half * y / divisions, top = -half + 2 * half * (y + 1) / divisions;
      const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
      const Vec3 normal = upward ? Vec3{0, 0, 1} : Vec3{0, 0, -1};
      for (const auto position :
           {Vec3{left, bottom, z}, Vec3{right, bottom, z}, Vec3{right, top, z}, Vec3{left, top, z}})
        mesh.vertices.push_back({position, normal});
      const std::uint32_t up[6] = {0, 1, 2, 0, 2, 3}, down[6] = {0, 2, 1, 0, 3, 2};
      for (int i = 0; i < 6; ++i)
        mesh.indices.push_back(base + (upward ? up[i] : down[i]));
    }
  return mesh;
}

LinearRGB estimate(const RayScene &scene, int samples = 4096)
{
  PathIntegrator integrator({4, 4});
  IndependentSampler sampler(42);
  LinearRGB total;
  for (int i = 0; i < samples; ++i) {
    sampler.start(0, static_cast<std::uint64_t>(i));
    total += integrator.Li({{0, 0, 1}, {0, 0, -1}}, scene, sampler);
  }
  return total / static_cast<float>(samples);
}

bool waitUntil(const std::function<bool()> &predicate)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!predicate()) {
    if (std::chrono::steady_clock::now() > deadline)
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

struct Control {
  std::atomic<int> calls = 0, cancelled = 0;
  std::mutex mutex;
  std::condition_variable ready;
  bool release = false;
  bool blockFirst = false;
  std::vector<Vec3> cameras;
  std::vector<std::uint64_t> sampleIndices, seeds;
};
class RecordingBackend final : public ProgressiveBackend {
public:
  explicit RecordingBackend(std::shared_ptr<Control> control, std::uint64_t limit = 1)
      : m_Control(std::move(control)), m_Limit(limit)
  {
  }
  ProgressiveSettings samplingSettings() const override
  {
    ProgressiveSettings s;
    s.maxSamples = m_Limit;
    s.minSamplesPerFrame = 1;
    return s;
  }
  void render(const RayScene &, const RenderRequest &request, Film &film) override
  {
    const int call = ++m_Control->calls;
    {
      std::unique_lock lock(m_Control->mutex);
      m_Control->cameras.push_back(request.camera.position);
      m_Control->sampleIndices.push_back(request.sampleIndex);
      m_Control->seeds.push_back(request.seed);
      if (call == 1 && m_Control->blockFirst) {
        while (!m_Control->release && !request.cancellation.stop_requested())
          m_Control->ready.wait_for(lock, std::chrono::milliseconds(1));
      }
    }
    if (request.cancellation.stop_requested()) {
      ++m_Control->cancelled;
      return;
    }
    film.addSample(std::vector<LinearRGB>(request.size.pixels(), LinearRGB{1, 0, 0}));
  }

private:
  std::shared_ptr<Control> m_Control;
  std::uint64_t m_Limit;
};
} // namespace

TEST(RayGeometry, NearestHitClippingBackFacesAndSharedResources)
{
  Fixture f;
  const auto first = f.add(MeshData::cube(2));
  const auto second = f.scene.world().create();
  second.add<Transform>().position = {0, 0, -4};
  second.add<Geometry>(first.get<Geometry>().resource);
  second.add<Material>();
  const auto scene = RayScene::build(f.snapshot());
  EXPECT_EQ(scene.meshCount(), 1u);
  SurfaceHit hit;
  ASSERT_TRUE(scene.intersect({{0, 0, 5}, {0, 0, -1}}, hit));
  EXPECT_NEAR(hit.t, 4, 1e-5);
  EXPECT_EQ(hit.entity, first.id());
  EXPECT_TRUE(hit.frontFace);
  EXPECT_EQ(hit.geometricNormal, (Vec3{0, 0, 1}));
  EXPECT_FALSE(scene.occluded({{0, 0, 5}, {0, 0, -1}, 0, 3.9f}));
  ASSERT_TRUE(scene.intersect({{0, 0, 0}, {0, 0, 1}}, hit));
  EXPECT_FALSE(hit.frontFace);
  EXPECT_NEAR(hit.t, 1, 1e-5);
  EXPECT_FALSE(scene.intersect({{0, 0, 5}, {1, 0, 0}}, hit));
  EXPECT_FALSE(RayScene{}.occluded({}));
}

TEST(RayGeometry, HierarchyShearAndNonuniformScaleMatchWorldGeometry)
{
  Fixture f;
  auto parent = f.scene.world().create();
  auto &pose = parent.add<Transform>();
  pose.scale = {2, 1, 0.75f};
  pose.rotation = Quaternion::angleAxis({0, 1, 0}, 25);
  const auto object = f.add(MeshData::cube(2), {}, {0.3f, 0, 0}, {0.8f, 1.2f, 0.6f});
  object.get<Transform>().rotation = Quaternion::angleAxis({0, 0, 1}, 35);
  f.scene.world().setParent(object, parent);
  const auto source = f.snapshot();
  const auto accelerated = RayScene::build(source);
  auto baked = source;
  MeshData mesh = source.instances[0].mesh->data();
  const auto model = source.instances[0].objectToWorld;
  const auto inverse = Jade::Renderer::Math::inverseAffine(model);
  for (auto &vertex : mesh.vertices) {
    vertex.position = Jade::Renderer::Math::point(model, vertex.position);
    vertex.normal = Jade::Renderer::Math::normal(inverse, vertex.normal);
  }
  baked.instances[0].mesh = std::make_shared<const Mesh>(Mesh::compile(std::move(mesh)));
  baked.instances[0].objectToWorld = Matrix4::Identity();
  const auto reference = RayScene::build(baked);
  IndependentSampler sampler;
  int hits = 0;
  for (int i = 0; i < 1000; ++i) {
    sampler.start(0, static_cast<std::uint64_t>(i));
    const auto u = sampler.next2D();
    const Ray ray{{0, 0, 6}, unit({(u.x - .5f) * 4, (u.y - .5f) * 4, -6})};
    SurfaceHit a, b;
    const bool ha = accelerated.intersect(ray, a), hb = reference.intersect(ray, b);
    ASSERT_EQ(ha, hb);
    EXPECT_EQ(accelerated.occluded(ray), ha);
    if (ha) {
      ++hits;
      EXPECT_NEAR(a.t, b.t, 2e-5);
      EXPECT_GT(dot(a.geometricNormal, b.geometricNormal), 0.9999f);
    }
  }
  EXPECT_GT(hits, 100);
}

TEST(RayGeometry, SnapshotsRetainMeshesAndIgnoreLaterEdits)
{
  SceneSnapshot snapshot;
  {
    Fixture f;
    auto entity = f.add(MeshData::cube(2));
    snapshot = f.snapshot();
    entity.get<Transform>().position = {100, 0, 0};
    entity.get<Material>().emission = {10, 0, 0};
    EXPECT_NE(snapshot, f.snapshot());
  }
  const auto detached = RayScene::build(snapshot);
  SurfaceHit hit;
  ASSERT_TRUE(detached.intersect({{0, 0, 5}, {0, 0, -1}}, hit));
  EXPECT_EQ(detached.material(hit).emission, (LinearRGB{}));
}

TEST(RayGeometry, DegenerateTrianglesAndInvalidBindings)
{
  Fixture f;
  auto object = f.add(MeshData::triangle({}, {1, 1, 1}, {2, 2, 2}));
  SurfaceHit hit;
  EXPECT_FALSE(RayScene::build(f.snapshot()).intersect({{0, 0, 5}, {0, 0, -1}}, hit));
  auto invalid = f.snapshot();
  invalid.instances[0].objectToWorld.at(3, 2) = 1;
  EXPECT_THROW(RayScene::build(invalid), std::invalid_argument);
  invalid.instances[0].objectToWorld = Matrix4::Identity();
  invalid.instances[0].objectToWorld.at(0, 3) = INFINITY;
  EXPECT_THROW(RayScene::build(invalid), std::invalid_argument);
  object.remove<Material>();
  EXPECT_THROW(f.snapshot(), std::invalid_argument);
  object.add<Material>();
  Assets other;
  object.get<Geometry>().resource = other.add(MeshData::cube());
  EXPECT_THROW(f.snapshot(), std::invalid_argument);
  object.get<Geometry>().resource = {};
  EXPECT_TRUE(f.snapshot().instances.empty());
}

TEST(RayGeometry, DescriptorGeometryCanBindClearAndRejectExpiredMeshes)
{
  Fixture f;
  const auto object = f.scene.world().create<Archetype<Transform, Geometry, Material>>();
  object.get<Transform>().scale = {};
  object.get<Material>().roughness = -1;
  EXPECT_TRUE(f.snapshot().instances.empty());
  object.get<Geometry>().resource = f.assets.add(MeshData::cube());
  EXPECT_THROW(f.snapshot(), std::invalid_argument);
  object.get<Transform>().scale = {1, 1, 1};
  object.get<Material>() = {};
  EXPECT_EQ(f.snapshot().instances.size(), 1u);
  object.get<Geometry>().resource = {};
  EXPECT_TRUE(f.snapshot().instances.empty());
  {
    Assets temporary;
    object.get<Geometry>().resource = temporary.add(MeshData::cube());
  }
  EXPECT_FALSE(object.get<Geometry>().resource.empty());
  EXPECT_FALSE(object.get<Geometry>().resource.valid());
  EXPECT_THROW(f.snapshot(), std::invalid_argument);
}

TEST(RayCamera, RaysFollowProjectionAspectAndPrimaryClipPlanes)
{
  CameraFrame camera;
  camera.position = {0, 0, 5};
  const auto center = camera.generateRay({80, 60}, {160, 120});
  EXPECT_EQ(center.direction, (Vec3{0, 0, -1}));
  EXPECT_FLOAT_EQ(center.tMin, 0.1f);
  const auto edge = camera.generateRay({160, 0}, {160, 120});
  EXPECT_GT(edge.direction.x, 0);
  EXPECT_GT(edge.direction.y, 0);
  EXPECT_NEAR(edge.tMin * (-edge.direction.z), camera.lens.nearPlane, 1e-6);
  camera.lens.projection = Projection::Orthographic;
  const auto left = camera.generateRay({0, 60}, {160, 120}),
             right = camera.generateRay({160, 60}, {160, 120});
  EXPECT_EQ(left.direction, right.direction);
  EXPECT_NEAR(right.origin.x - left.origin.x, 8, 1e-5);
  EXPECT_THROW(camera.generateRay({0, 0}, {0, 1}), std::invalid_argument);
}

TEST(BSDF, DiffuseSamplingMetalReflectionAndPdfAgreement)
{
  IndependentSampler sampler(5);
  Material material;
  material.baseColor = {0.8f, 0.5f, 0.2f};
  const BSDF diffuse(material, flatHit());
  LinearRGB mean;
  for (int i = 0; i < 1000; ++i) {
    sampler.start(0, static_cast<std::uint64_t>(i));
    const auto sample = diffuse.sample({0, 0, 1}, sampler);
    ASSERT_GT(sample.pdf, 0);
    EXPECT_NEAR(sample.pdf, diffuse.pdf({0, 0, 1}, sample.direction), 1e-6);
    mean += sample.value * (sample.direction.z / sample.pdf);
  }
  mean = mean / 1000;
  EXPECT_NEAR(mean.r, material.baseColor.r, 1e-5);
  material.model = MaterialModel::Metal;
  material.roughness = 0;
  const auto reflection = BSDF(material, flatHit()).sample(unit({.3f, 0, 1}), sampler);
  EXPECT_TRUE(reflection.delta);
  EXPECT_LT(reflection.direction.x, 0);
  EXPECT_GT(reflection.direction.z, 0);
  material.roughness = .3f;
  const BSDF rough(material, flatHit());
  for (int i = 0; i < 1000; ++i) {
    sampler.start(1, static_cast<std::uint64_t>(i));
    const auto sample = rough.sample({0, 0, 1}, sampler);
    EXPECT_TRUE(sample.value.finite());
    EXPECT_NEAR(sample.pdf, rough.pdf({0, 0, 1}, sample.direction), 1e-4);
  }
}

TEST(BSDF, GlassTransmissionTotalInternalReflectionAndEqualIor)
{
  Material glass;
  glass.model = MaterialModel::Dielectric;
  glass.roughness = 0;
  glass.baseColor = {1, 1, 1};
  IndependentSampler sampler;
  int transmitted = 0, reflected = 0;
  for (int i = 0; i < 1000; ++i) {
    sampler.start(0, static_cast<std::uint64_t>(i));
    const auto sample = BSDF(glass, flatHit()).sample({0, 0, 1}, sampler);
    if (sample.transmission) {
      ++transmitted;
      EXPECT_LT(sample.direction.z, 0);
      EXPECT_FLOAT_EQ(sample.eta, 1.5f);
    } else {
      ++reflected;
      EXPECT_GT(sample.direction.z, 0);
    }
  }
  EXPECT_GT(transmitted, 900);
  EXPECT_GT(reflected, 10);
  const auto internal = BSDF(glass, flatHit(false)).sample(unit({.9f, 0, -.3f}), sampler);
  EXPECT_FALSE(internal.transmission);
  EXPECT_TRUE(internal.delta);
  EXPECT_LT(internal.direction.z, 0);
  glass.roughness = .3f;
  const BSDF rough(glass, flatHit());
  for (int i = 0; i < 1000; ++i) {
    sampler.start(0, static_cast<std::uint64_t>(i));
    const auto sample = rough.sample({0, 0, 1}, sampler);
    EXPECT_TRUE(sample.value.finite());
    EXPECT_NEAR(sample.pdf, rough.pdf({0, 0, 1}, sample.direction), 1e-4);
  }
  glass.ior = 1;
  const auto clear = BSDF(glass, flatHit()).sample({0, 0, 1}, sampler);
  EXPECT_TRUE(clear.transmission);
  EXPECT_TRUE(clear.delta);
  EXPECT_FLOAT_EQ(clear.pdf, 1);
}

TEST(PathTracing, NoEmittersMeansBlackAndEmissionIsDirectionalHdr)
{
  Fixture f;
  auto object = f.add(MeshData::cube(2));
  IndependentSampler sampler;
  PathIntegrator integrator;
  const Ray ray{{0, 0, 5}, {0, 0, -1}};
  EXPECT_EQ(integrator.Li(ray, RayScene::build(f.snapshot()), sampler), (LinearRGB{}));
  object.get<Material>().emission = {12, 4, 2};
  auto scene = RayScene::build(f.snapshot());
  const auto value = integrator.Li(ray, scene, sampler);
  EXPECT_NEAR(value.r, 12, 1e-5);
  EXPECT_NEAR(value.g, 4, 1e-5);
  EXPECT_EQ(integrator.Li({{0, 0, 0}, {0, 0, 1}}, scene, sampler), (LinearRGB{}));
  object.get<Material>().twoSidedEmission = true;
  scene = RayScene::build(f.snapshot());
  EXPECT_GT(integrator.Li({{0, 0, 0}, {0, 0, 1}}, scene, sampler).r, 0);
}

TEST(PathTracing, EmissiveMeshSubdivisionPreservesEnergyAndShadowsOcclude)
{
  Fixture coarse, fine;
  Material diffuse;
  diffuse.baseColor = {.7f, .7f, .7f};
  Material emission;
  emission.baseColor = {0, 0, 0};
  emission.emission = {8, 8, 8};
  coarse.add(quad(5, 0), diffuse);
  fine.add(quad(5, 0), diffuse);
  coarse.add(quad(1, 2, false), emission);
  fine.add(quad(1, 2, false, 4), emission);
  const auto a = estimate(RayScene::build(coarse.snapshot()));
  const auto b = estimate(RayScene::build(fine.snapshot()));
  EXPECT_GT(a.r, .5f);
  EXPECT_NEAR(a.r, b.r, .05f * a.r);
  coarse.add(quad(5, 1.5f), Material{});
  const auto blocked = estimate(RayScene::build(coarse.snapshot()));
  EXPECT_LT(blocked.r, .01f);
}

TEST(PathTracing, IndirectEmissionSurvivesBeyondCameraFarPlane)
{
  Fixture f;
  Material diffuse;
  diffuse.baseColor = {1, 1, 1};
  f.add(quad(5, 0), diffuse);
  Material light;
  light.baseColor = {0, 0, 0};
  light.emission = {10, 10, 10};
  f.add(quad(2, 4, false), light);
  IndependentSampler sampler;
  sampler.start(0, 0);
  const auto value =
      PathIntegrator({4, 4}).Li({{0, 0, 1}, {0, 0, -1}, .1f, 2}, RayScene::build(f.snapshot()), sampler);
  EXPECT_GT(value.r, 0);
}

TEST(RayTracing, HeadlessRenderingIsDeterministicAcrossWorkerCounts)
{
  Fixture f;
  Material light;
  light.emission = {4, 2, 1};
  light.baseColor = {0, 0, 0};
  f.add(MeshData::sphere(1, 16, 8), light);
  const auto snapshot = f.snapshot();
  const auto scene = RayScene::build(snapshot);
  Jade::JobSystem one({1}), many({3});
  RayTracing a(one), b(many);
  Film fa({48, 32}), fb({48, 32});
  for (std::uint64_t sample = 0; sample < 4; ++sample) {
    const RenderRequest request{*snapshot.camera, fa.size(), sample, 91, {}};
    a.render(scene, request, fa);
    b.render(scene, request, fb);
  }
  EXPECT_EQ(fa.samples(), 4u);
  EXPECT_EQ(fa.resolve().pixels, fb.resolve().pixels);
  EXPECT_GT(fa.pixels()[16 * 48 + 24].r, 1);
  const auto before = fa.resolve().pixels;
  std::stop_source cancelled;
  cancelled.request_stop();
  a.render(scene, {*snapshot.camera, fa.size(), 4, 91, cancelled.get_token()}, fa);
  EXPECT_EQ(fa.samples(), 4u);
  EXPECT_EQ(fa.resolve().pixels, before);
  EXPECT_THROW(a.render(scene, {*snapshot.camera, fa.size(), 0, 0, {}}, fa), std::invalid_argument);
}

TEST(RayTracing, SharedJobsCancelInFlightTilesAndDrainFailuresBeforeReturning)
{
  class Gate final : public Integrator {
  public:
    mutable std::latch entered{1};
    mutable std::binary_semaphore release{0};
    mutable std::atomic<bool> first = true;
    bool fail = false;
    LinearRGB Li(const Ray&, const RayScene&, Sampler&) const override {
      if (first.exchange(false)) { entered.count_down(); release.acquire(); }
      if (fail) throw std::runtime_error("transport failure");
      return {1, 0, 0};
    }
  };
  for (bool failure : {false, true}) {
    JobSystem jobs({1}); RayTracing renderer(jobs);
    auto integrator = std::make_unique<Gate>(); auto* gate = integrator.get(); gate->fail = failure;
    renderer.setIntegrator(std::move(integrator));
    Film film({32, 32}); std::stop_source cancel;
    auto render = jobs.submit([&] { renderer.render(RayScene{}, {{}, film.size(), 0, 0, cancel.get_token()}, film); },
                              JobCategory::RayTracing);
    gate->entered.wait(); cancel.request_stop(); gate->release.release();
    if (failure) EXPECT_THROW(jobs.wait(render), std::runtime_error);
    else EXPECT_NO_THROW(jobs.wait(render));
    EXPECT_EQ(film.samples(), 0u);
    jobs.wait(jobs.submit([] {}, JobCategory::Physics));
  }
}

TEST(Film, HdrMeanToneMappingAndValidation)
{
  Film film({1, 1});
  const LinearRGB first[]{{3, 0, 0}}, second[]{{1, 0, 0}};
  film.addSample(first);
  film.addSample(second);
  EXPECT_FLOAT_EQ(film.pixels()[0].r, 2);
  EXPECT_EQ(film.resolve().pixels[3], 255);
  EXPECT_GT(film.resolve().pixels[0], 200);
  EXPECT_EQ(film.resolve().pixels[1], 0);
  EXPECT_GT(film.resolve(1).pixels[0], film.resolve().pixels[0]);
  const LinearRGB bad[]{{INFINITY, 0, 0}};
  EXPECT_THROW(film.addSample(bad), std::runtime_error);
  EXPECT_EQ(film.samples(), 2u);
  EXPECT_THROW((Film{{0, 1}}), std::invalid_argument);
  Material material;
  material.emission.r = -1;
  EXPECT_THROW(validateMaterial(material), std::invalid_argument);
  RayTracingSettings invalid;
  invalid.resolutionScale = 0;
  EXPECT_THROW((RayTracing{invalid}), std::invalid_argument);
}

TEST(ProgressiveSession, LiveExposureAndSampleLimitsPreserveFilmWhileSeedAndResolutionReset)
{
  Fixture f;
  auto control = std::make_shared<Control>();
  Jade::Detail::ProgressiveSession session;
  session.install(std::make_unique<RecordingBackend>(control));
  session.start();
  const auto snapshot = f.snapshot();
  session.submit(snapshot, {8, 8});
  ASSERT_TRUE(waitUntil([&] { return session.progress().samplesPerPixel == 1; }));
  const auto first = session.image();
  const auto revision = session.progress().revision;
  ProgressiveSettings settings; settings.maxSamples = 1; settings.exposureEV = 2;
  session.updateSettings(settings);
  ASSERT_TRUE(waitUntil([&] { return session.image() != first; }));
  EXPECT_GT(session.image()->pixels[0], first->pixels[0]);
  EXPECT_EQ(control->calls, 1);
  EXPECT_EQ(session.progress().samplesPerPixel, 1u);
  EXPECT_EQ(session.progress().revision, revision);
  settings.maxSamples = 3;
  session.updateSettings(settings);
  ASSERT_TRUE(waitUntil([&] { return session.progress().samplesPerPixel == 3; }));
  EXPECT_EQ(control->calls, 3);
  EXPECT_EQ(session.progress().revision, revision);
  settings.maxSamples = 1;
  session.updateSettings(settings);
  settings.seed = 17;
  session.updateSettings(settings);
  session.submit(snapshot, {8, 8});
  ASSERT_TRUE(waitUntil([&] { return session.progress().samplesPerPixel == 1; }));
  EXPECT_GT(session.progress().revision, revision);
  EXPECT_EQ(control->calls, 4);
  settings.resolutionScale = .5f;
  session.updateSettings(settings);
  session.submit(snapshot, {8, 8});
  ASSERT_TRUE(waitUntil([&] { return session.progress().samplesPerPixel == 1; }));
  EXPECT_EQ(session.progress().size, (ImageSize{4, 4}));
  EXPECT_EQ(control->calls, 5);
  session.stop();
  session.rethrowError();
}

TEST(ProgressiveSession, ExposureUpdatedDuringARoundResolvesTheCompletedSampleWithoutRetracing)
{
  Fixture f;
  auto control = std::make_shared<Control>(); control->blockFirst = true;
  Jade::Detail::ProgressiveSession session;
  session.install(std::make_unique<RecordingBackend>(control));
  session.start(); session.submit(f.snapshot(), {4, 4});
  ASSERT_TRUE(waitUntil([&] { return control->calls == 1; }));
  ProgressiveSettings settings; settings.maxSamples = 1; settings.exposureEV = 1;
  session.updateSettings(settings);
  {
    std::lock_guard lock(control->mutex);
    control->release = true;
  }
  control->ready.notify_all();
  ASSERT_TRUE(waitUntil([&] { return session.progress().samplesPerPixel == 1; }));
  Film expected({4, 4}); expected.addSample(std::vector<LinearRGB>(16, LinearRGB{1, 0, 0}));
  ASSERT_TRUE(session.image());
  EXPECT_EQ(session.image()->pixels, expected.resolve(1).pixels);
  EXPECT_EQ(control->calls, 1);
  EXPECT_FALSE(session.progress().inFlight);
  session.stop(); session.rethrowError();
}

TEST(Film, NormalDataBypassesExposureToneMappingAndSrgb)
{
  Film film({1, 1});
  const std::array sample{LinearRGB{.5f, .5f, 1}};
  film.addSample(sample);
  EXPECT_EQ(film.resolve(0, ImageEncoding::LinearData).pixels,
    (std::vector<std::uint8_t>{128, 128, 255, 255}));
  EXPECT_EQ(film.resolve(4, ImageEncoding::LinearData).pixels,
    film.resolve(0, ImageEncoding::LinearData).pixels);
}

TEST(ProgressiveSession, RunningFramesFinishWholeRoundsBeforeAdvancingAndOnlyPauseAccumulates)
{
  Fixture f;
  auto control = std::make_shared<Control>(); control->blockFirst = true;
  Jade::Detail::ProgressiveSession session;
  session.install(std::make_unique<RecordingBackend>(control, 3));
  session.start();
  const auto snapshot = f.snapshot();
  const auto expired = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  session.submit(snapshot, {8, 8}, false, expired);
  ASSERT_TRUE(waitUntil([&] { return control->calls == 1; }));
  std::atomic<bool> completed = false;
  std::jthread waiter([&] { session.waitForFrame(); completed = true; });
  // Even an expired budget cannot expose an unfinished whole-image round.
  EXPECT_FALSE(completed);
  EXPECT_FALSE(session.image());
  {
    std::lock_guard lock(control->mutex); control->release = true;
  }
  control->ready.notify_all();
  const bool finished = waitUntil([&] { return completed.load(); });
  if (!finished) session.stop();
  waiter.join();
  ASSERT_TRUE(finished);
  ASSERT_TRUE(session.image());
  EXPECT_EQ(session.image()->pixels.size(), 8u * 8u * 4u);
  EXPECT_EQ(session.progress().samplesPerPixel, 1u);
  EXPECT_EQ(control->calls, 1);
  const auto firstRevision = session.progress().revision;

  // An unchanged scene still gets a fresh frame and a different random stream.
  session.submit(snapshot, {8, 8}, false, expired);
  ASSERT_TRUE(waitUntil([&] { return session.progress().revision > firstRevision; }));
  session.waitForFrame();
  EXPECT_EQ(session.progress().samplesPerPixel, 1u);
  EXPECT_EQ(control->calls, 2);
  session.submit(snapshot, {8, 8}, true);
  ASSERT_TRUE(waitUntil([&] { return session.progress().samplesPerPixel == 3; }));
  const auto pausedRevision = session.progress().revision;
  session.submit(snapshot, {8, 8}, true);
  EXPECT_EQ(control->calls, 5);
  EXPECT_EQ(session.progress().revision, pausedRevision);
  session.submit(snapshot, {8, 8}, false, expired);
  ASSERT_TRUE(waitUntil([&] { return session.progress().revision > pausedRevision; }));
  session.waitForFrame();
  EXPECT_EQ(session.progress().samplesPerPixel, 1u);
  session.stop(); session.rethrowError();
  EXPECT_EQ(control->sampleIndices, (std::vector<std::uint64_t>{0, 0, 0, 1, 2, 0}));
  ASSERT_EQ(control->seeds.size(), 6u);
  EXPECT_NE(control->seeds[0], control->seeds[1]);
  EXPECT_EQ(control->seeds[2], 0u);
  EXPECT_EQ(control->seeds[2], control->seeds[4]);
}

TEST(ProgressiveSession, FrameQualityMinimumSurvivesExpiredBudgetsAndHonorsTheSampleCap)
{
  Fixture f; auto control = std::make_shared<Control>();
  Jade::Detail::ProgressiveSession session;
  session.install(std::make_unique<RecordingBackend>(control, 0));
  ProgressiveSettings settings; settings.minSamplesPerFrame = 8;
  session.updateSettings(settings); session.start();
  const auto expired = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  session.submit(f.snapshot(), {4,4}, false, expired);
  ASSERT_TRUE(waitUntil([&] { return session.progress().samplesPerPixel == 8; }));
  session.waitForFrame();
  EXPECT_EQ(control->calls, 8);
  const auto completed = session.progress();
  settings.minSamplesPerFrame = 0;
  EXPECT_THROW(session.updateSettings(settings), std::invalid_argument);
  EXPECT_EQ(session.progress().revision, completed.revision);
  settings.minSamplesPerFrame = 12; settings.maxSamples = 3;
  session.updateSettings(settings);
  session.submit(f.snapshot(), {4,4}, false, expired);
  ASSERT_TRUE(waitUntil([&] { return session.progress().revision > completed.revision && session.progress().samplesPerPixel == 3; }));
  session.waitForFrame(); session.stop(); session.rethrowError();
  EXPECT_EQ(control->calls, 11);
}

TEST(ProgressiveSession, RunningFrameBudgetAllowsAdditionalCompletedSamplesWithinOneFrame)
{
  Fixture f;
  auto control = std::make_shared<Control>();
  Jade::Detail::ProgressiveSession session;
  session.install(std::make_unique<RecordingBackend>(control, 3)); session.start();
  session.submit(f.snapshot(), {4, 4}, false, std::chrono::steady_clock::now() + std::chrono::seconds(5));
  ASSERT_TRUE(waitUntil([&] { return session.progress().samplesPerPixel == 3; }));
  session.waitForFrame();
  session.stop(); session.rethrowError();
  EXPECT_EQ(control->calls, 3);
  EXPECT_EQ(control->sampleIndices, (std::vector<std::uint64_t>{0, 1, 2}));
  ASSERT_EQ(control->seeds.size(), 3u);
  EXPECT_EQ(control->seeds[0], control->seeds[2]);
}

TEST(ProgressiveSession, CoalescesMovingSnapshotsAndResetsAccumulation)
{
  Fixture f;
  auto control = std::make_shared<Control>();
  control->blockFirst = true;
  Jade::Detail::ProgressiveSession session;
  session.install(std::make_unique<RecordingBackend>(control));
  session.start();
  session.submit(f.snapshot(), {8, 8});
  ASSERT_TRUE(waitUntil([&] { return control->calls.load() == 1; }));
  f.camera.get<Transform>().position.x = 1;
  session.submit(f.snapshot(), {8, 8});
  f.camera.get<Transform>().position.x = 2;
  session.submit(f.snapshot(), {8, 8});
  {
    std::lock_guard lock(control->mutex);
    control->release = true;
  }
  control->ready.notify_all();
  ASSERT_TRUE(waitUntil([&] { return session.progress().revision == 3; }));
  session.stop();
  session.rethrowError();
  ASSERT_EQ(control->cameras.size(), 2u);
  EXPECT_EQ(control->cameras.back(), (Vec3{2, 0, 5}));
  EXPECT_EQ(session.progress().samplesPerPixel, 1u);
  EXPECT_EQ(session.image()->size, (ImageSize{8, 8}));
}

TEST(ProgressiveSession, InvalidationsCancelAndResizeCannotPublishStaleImages)
{
  Fixture f;
  auto control = std::make_shared<Control>();
  control->blockFirst = true;
  Jade::Detail::ProgressiveSession session;
  session.install(std::make_unique<RecordingBackend>(control));
  session.start();
  session.submit(f.snapshot(), {8, 8});
  ASSERT_TRUE(waitUntil([&] { return control->calls.load() == 1; }));
  session.invalidate();
  ASSERT_TRUE(waitUntil([&] { return control->cancelled.load() == 1; }));
  EXPECT_FALSE(session.image());
  session.submit(f.snapshot(), {12, 7});
  ASSERT_TRUE(waitUntil([&] { return session.progress().samplesPerPixel == 1; }));
  session.stop();
  EXPECT_EQ(session.image()->size, (ImageSize{12, 7}));
}

TEST(ProgressiveSession, PropagatesBackendFailuresAndEnforcesRoundContract)
{
  class Broken final : public ProgressiveBackend {
  public:
    void render(const RayScene &, const RenderRequest &, Film &) override
    {
      throw std::runtime_error("worker failure");
    }
  };
  Fixture f;
  Jade::Detail::ProgressiveSession session;
  session.install(std::make_unique<Broken>());
  session.start();
  session.submit(f.snapshot(), {8, 8});
  bool failed = false;
  ASSERT_TRUE(waitUntil([&] {
    try {
      session.rethrowError();
    } catch (const std::runtime_error &) {
      failed = true;
    }
    return failed;
  }));
  session.stop();
}

TEST(OpenGLDisplay, PresentsSrgbPixelsWithCorrectImageOrientation)
{
  WindowConfig config;
  config.visible = false;
  config.width = 64;
  config.height = 64;
  Jade::Window window(config);
  Jade::Detail::OpenGLDevice device(window, false);
  Jade::Detail::OpenGLDisplay display(device);
  Jade::Detail::DebugDraw debug(device);
  display.begin({64, 64});
  const Image image{{2, 2}, {255, 0, 0, 255, 255, 0, 0, 255, 0, 0, 255, 255, 0, 0, 255, 255}};
  display.image(image);
  std::array<unsigned char, 4> top{}, bottom{};
  glReadBuffer(GL_BACK);
  glReadPixels(32, 60, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, top.data());
  glReadPixels(32, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bottom.data());
  EXPECT_GT(top[0], 250);
  EXPECT_LT(top[2], 5);
  EXPECT_GT(bottom[2], 250);
  EXPECT_LT(bottom[0], 5);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  display.present();
}

TEST(ApplicationRendering, WireframeBypassesBackendAndResumesCurrentScene)
{
  EngineConfig config;
  config.window.visible = false;
  config.window.width = 64;
  config.window.height = 64;
  config.debug.axes = false;
  config.logPerformance = false;
  config.debug.shortcuts = true;
  config.jobs.workerCount = 1;
  config.renderer.rayTracing.maxSamples = 1;
  Application app(config);
  auto control = std::make_shared<Control>();
  class CountingIntegrator final : public Integrator {
  public:
    explicit CountingIntegrator(std::shared_ptr<Control> control) : control(std::move(control)) {}
    LinearRGB Li(const Ray &, const RayScene &, Sampler &) const override
    {
      ++control->calls;
      return {1, 0, 0};
    }
    std::shared_ptr<Control> control;
  };
  app.runtime().renderer().rayTracing().setIntegrator(std::make_unique<CountingIntegrator>(control));
  auto scene = std::make_shared<Scene>(std::make_shared<World>());
  auto camera = scene->world().create();
  camera.add<Transform>().position = {0, 0, 5};
  camera.add<Camera>();
  scene->setCamera(camera);
  auto object = scene->world().create();
  object.add<Transform>();
  object.add<Geometry>(app.runtime().assets().add(MeshData::cube(2)));
  object.add<Material>();
  app.runtime().setScene(scene);
  int pausedCalls = 0;
  bool resumed = false;
  app.on<FrameUpdate>([&](const auto &frame) {
    if (frame.frame == 3) {
      ASSERT_TRUE(waitUntil([&] { return control->calls.load() > 0; }));
      app.runtime().renderer().setRayTracingEnabled(false); app.runtime().renderer().setWireframe(true);
    }
    if (frame.frame == 4)
      pausedCalls = control->calls;
    if (frame.frame == 5) {
      EXPECT_EQ(control->calls.load(), pausedCalls);
      object.get<Transform>().position = {1, 0, 0};
    }
    if (frame.frame == 7)
      app.runtime().renderer().setRayTracingEnabled(true); app.runtime().renderer().setWireframe(false);
    if (frame.frame == 10)
      resumed = waitUntil([&] { return control->calls.load() > pausedCalls; });
  });
  app.Run({10});
  EXPECT_GT(pausedCalls, 0);
  EXPECT_TRUE(resumed);
  EXPECT_FALSE(app.runtime().renderer().wireframeEnabled());
}

TEST(ApplicationRendering, OwnsDefaultRayTracingAndLocksIntegratorDuringRun)
{
  EngineConfig config;
  config.window.visible = false;
  config.window.width = 64;
  config.window.height = 64;
  config.jobs.workerCount = 1;
  Application app(config);
  EXPECT_EQ(app.runtime().renderer().rayTracing().name(), "RayTracing");
  EXPECT_FALSE(app.runtime().renderer().wireframeEnabled());
  app.on<FrameUpdate>([&] {
    EXPECT_THROW(app.runtime().renderer().rayTracing().setIntegrator(std::make_unique<PathIntegrator>()), std::logic_error);
  });
  EXPECT_NO_THROW(app.Run({1}));
  app.runtime().renderer().setRayTracingEnabled(false); app.runtime().renderer().setWireframe(true);
  EXPECT_NO_THROW(app.Run({1}));
  EXPECT_NO_THROW(app.runtime().renderer().rayTracing().setIntegrator(std::make_unique<PathIntegrator>()));
}

TEST(PathTracing, DiffuseWallReturnsIndirectColoredLight)
{
  Fixture f;
  Material neutral;
  neutral.baseColor = {.7f, .7f, .7f};
  f.add(quad(5, 0), neutral);
  Material light;
  light.baseColor = {0, 0, 0};
  light.emission = {8, 8, 8};
  f.add(quad(.75f, 3, false), light);
  Material red;
  red.baseColor = {.9f, 0, 0};
  MeshData wall;
  wall.vertices = {
      {{2, -2, 0}, {-1, 0, 0}}, {{2, -2, 3}, {-1, 0, 0}}, {{2, 2, 3}, {-1, 0, 0}}, {{2, 2, 0}, {-1, 0, 0}}};
  wall.indices = {0, 1, 2, 0, 2, 3};
  f.add(wall, red);
  const auto scene = RayScene::build(f.snapshot());
  PathIntegrator direct({1, 4}), indirect({4, 4});
  IndependentSampler a(42), b(42);
  LinearRGB da, db;
  for (std::uint64_t i = 0; i < 8192; ++i) {
    a.start(0, i);
    b.start(0, i);
    da += direct.Li({{0, 0, 1}, {0, 0, -1}}, scene, a);
    db += indirect.Li({{0, 0, 1}, {0, 0, -1}}, scene, b);
  }
  da = da / 8192;
  db = db / 8192;
  EXPECT_GT(db.r - da.r, .01f);
  EXPECT_NEAR(db.b, da.b, 1e-6);
}

TEST(RayGeometry, SahBvhAgreesWithIndependentBruteForce)
{
  Fixture f;
  for (int i = 0; i < 24; ++i)
    f.add(MeshData::cube(1), {},
          {static_cast<float>((i % 6) - 3) * 1.5f, static_cast<float>(i / 6 - 2) * 1.5f,
           -static_cast<float>(i % 3)});
  const auto snapshot = f.snapshot();
  const auto scene = RayScene::build(snapshot);
  IndependentSampler sampler(817);
  for (std::uint64_t r = 0; r < 400; ++r) {
    sampler.start(0, r);
    const auto uv = sampler.next2D();
    Ray ray{{0, 0, 8}, unit({(uv.x - .5f) * 12, (uv.y - .5f) * 9, -8})};
    double nearest = INFINITY;
    for (const auto &instance : snapshot.instances) {
      const auto &mesh = instance.mesh->data();
      for (std::size_t p = 0; p < mesh.indices.size(); p += 3) {
        const auto a =
            Jade::Renderer::Math::point(instance.objectToWorld, mesh.vertices[mesh.indices[p]].position);
        const auto b =
            Jade::Renderer::Math::point(instance.objectToWorld, mesh.vertices[mesh.indices[p + 1]].position);
        const auto c =
            Jade::Renderer::Math::point(instance.objectToWorld, mesh.vertices[mesh.indices[p + 2]].position);
        const auto e1 = b - a, e2 = c - a, h = cross(ray.direction, e2);
        const double determinant = dot(e1, h);
        if (std::abs(determinant) < 1e-10)
          continue;
        const auto displacement = ray.origin - a;
        const double u = dot(displacement, h) / determinant;
        const auto q = cross(displacement, e1);
        const double v = dot(ray.direction, q) / determinant, t = dot(e2, q) / determinant;
        if (u >= 0 && v >= 0 && u + v <= 1 && t > ray.tMin && t <= ray.tMax)
          nearest = std::min(nearest, t);
      }
    }
    SurfaceHit hit;
    const bool found = scene.intersect(ray, hit);
    ASSERT_EQ(found, std::isfinite(nearest));
    if (found)
      EXPECT_NEAR(hit.t, nearest, 2e-5);
  }
}

TEST(OpenGLDisplay, WireframeAndAxesAreIndependentUnlitDiagnostics)
{
  Fixture f;
  f.add(MeshData::cube(2));
  const auto snapshot = f.snapshot();
  WindowConfig config;
  config.visible = false;
  config.width = 64;
  config.height = 64;
  Jade::Window window(config);
  Jade::Detail::OpenGLDevice device(window, false);
  Jade::Detail::OpenGLDisplay display(device);
  Jade::Detail::DebugDraw debug(device);
  display.begin({64, 64});
  debug.wireframe(snapshot, {64, 64});
  std::vector<unsigned char> pixels(64 * 64 * 4);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  int edges = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4)
    if (pixels[i] > 100)
      ++edges;
  EXPECT_GT(edges, 50);
  EXPECT_LT(edges, 1000);
  display.begin({64, 64});
  debug.axes(*snapshot.camera, {64, 64});
  glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  int red = 0, green = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    if (pixels[i] > 200 && pixels[i + 1] < 50)
      ++red;
    if (pixels[i + 1] > 200 && pixels[i] < 50)
      ++green;
  }
  EXPECT_GT(red, 4);
  EXPECT_GT(green, 4);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(RayTracing, CustomIntegratorWorksHeadlesslyAndLocksDuringApplicationRun)
{
  class Constant final : public Integrator {
  public:
    LinearRGB Li(const Ray &, const RayScene &, Sampler &) const override { return {.25f, .5f, .75f}; }
  };
  RayTracing renderer;
  renderer.setIntegrator(std::make_unique<Constant>());
  Film film({4, 4});
  renderer.render(RayScene{}, {{}, film.size(), 0, 0, {}}, film);
  EXPECT_EQ(film.pixels()[0], (LinearRGB{.25f, .5f, .75f}));
  EngineConfig config;
  config.window.visible = false;
  config.window.width = 64;
  config.window.height = 64;
  Application app(config);
  auto &registered = app.runtime().renderer().rayTracing();
  app.on<FrameUpdate>(
      [&] { EXPECT_THROW(registered.setIntegrator(std::make_unique<Constant>()), std::logic_error); });
  app.Run({1});
  EXPECT_NO_THROW(registered.setIntegrator(std::make_unique<Constant>()));
}

TEST(RendererTests, CancelledSceneCompilationCanBeRetried)
{
  Fixture fixture;
  fixture.add(MeshData::cube());
  RaySceneCompiler compiler;
  std::stop_source cancellation;
  cancellation.request_stop();
  EXPECT_EQ(compiler.compile(fixture.snapshot(), cancellation.get_token()).instanceCount(), 0u);
  const auto scene = compiler.compile(fixture.snapshot());
  EXPECT_EQ(scene.instanceCount(), 1u);
  SurfaceHit hit;
  EXPECT_TRUE(scene.intersect({{0, 0, 5}, {0, 0, -1}}, hit));
}
