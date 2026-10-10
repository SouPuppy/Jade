#include <Jade/Jade.hpp>
#include "internal/Platform/Window.hpp"
#include "internal/Renderer/Device/OpenGL/OpenGLDevice.hpp"
#include "internal/Renderer/Presentation/OpenGLDisplay.hpp"
#include "internal/Renderer/RasterizationAccess.hpp"
#include <gtest/gtest.h>
#include <glad/gl.h>
#include <cmath>
#include <limits>
namespace {
using namespace Jade;
using namespace Jade::Renderer;
WindowConfig hidden()
{
  WindowConfig c;
  c.visible = false;
  c.width = c.height = 64;
  return c;
}
MeshData quad(float half, float z, bool front = true)
{
  MeshData m;
  Vec3 n{0, 0, front ? 1.0f : -1.0f};
  m.vertices = {{{-half, -half, z}, n}, {{half, -half, z}, n}, {{half, half, z}, n}, {{-half, half, z}, n}};
  m.indices =
      front ? std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3} : std::vector<std::uint32_t>{0, 2, 1, 0, 3, 2};
  return m;
}
RasterizationConfig quick()
{
  RasterizationConfig c;
  c.areaSamples = 16;
  c.shadowResolution = 64;
  c.reflectionProbeResolution = 16;
  c.maxReflectionProbes = 2;
  return c;
}
struct GPU {
  Window window{hidden()};
  Detail::OpenGLDevice device{window, false};
  Detail::OpenGLFrameTarget target{device};
  Assets assets;
  Scene scene;
  Entity camera = scene.create();
  GPU()
  {
    camera.add<Transform>().position = {0, 0, 5};
    camera.add<Camera>();
    scene.setCamera(camera);
    target.resize({64, 64});
  }
  Entity add(MeshData data, Material material, Vec3 p = {}, Vec3 scale = {1, 1, 1})
  {
    auto e = scene.create();
    auto &t = e.add<Transform>();
    t.position = p;
    t.scale = scale;
    e.add<Geometry>(assets.add(std::move(data)));
    e.add<Material>(material);
    return e;
  }
  std::vector<LinearRGB> render(Rasterization &renderer)
  {
    auto s = SceneSnapshot::capture(scene, assets);
    renderer.render(s, {target.size(), 1, 0}, target);
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    std::vector<LinearRGB> pixels(target.size().pixels());
    glReadPixels(0, 0, 64, 64, GL_RGB, GL_FLOAT, pixels.data());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    for (auto rgb : pixels) {
      EXPECT_TRUE(rgb.finite());
      EXPECT_GE(std::min({rgb.r, rgb.g, rgb.b}), 0);
    }
    return pixels;
  }
};
LinearRGB center(const std::vector<LinearRGB> &p) { return p[32 * 64 + 32]; }
float difference(const std::vector<LinearRGB> &a, const std::vector<LinearRGB> &b)
{
  float sum = 0;
  for (std::size_t i = 0; i < a.size(); ++i)
    sum += std::abs(a[i].r - b[i].r) + std::abs(a[i].g - b[i].g) + std::abs(a[i].b - b[i].b);
  return sum / static_cast<float>(a.size());
}
} // namespace
TEST(Rasterization, ConfigurationAndTargetsValidateBeforeRendering)
{
  RasterizationConfig c;
  c.areaSamples = 0;
  EXPECT_THROW(Rasterization r(c), std::invalid_argument);
  c = quick();
  c.reflectionSteps = 513;
  EXPECT_THROW(Rasterization r(c), std::invalid_argument);
  class Memory final : public FrameTarget {
  public:
    ImageSize size() const noexcept override { return {1, 1}; }
    void clear(LinearRGB) override {}
    void upload(std::span<const LinearRGB>) override {}
  } target;
  Rasterization r;
  EXPECT_THROW(r.render({}, {{1, 1}, 0, 0}, target), std::invalid_argument);
  GPU gpu;
  auto snapshot = SceneSnapshot::capture(gpu.scene, gpu.assets);
  EXPECT_THROW(r.render(snapshot, {{2, 2}, 0, 0}, gpu.target), std::invalid_argument);
  gpu.add(quad(1, 0), Materials::Diffuse());
  snapshot = SceneSnapshot::capture(gpu.scene, gpu.assets);
  snapshot.instances[0].objectToWorld.at(0, 0) = -1;
  EXPECT_THROW(r.render(snapshot, {{64, 64}, 0, 0}, gpu.target), std::invalid_argument);
}
TEST(Rasterization, EmissionIsTheOnlyLightAndHdrUsesTheSharedDisplayTransform)
{
  GPU gpu;
  auto e = gpu.add(quad(4, 0), Materials::Diffuse());
  Rasterization renderer(quick());
  EXPECT_EQ(center(gpu.render(renderer)), LinearRGB{});
  e.get<Material>() = Materials::Emissive({4, 2, 1});
  const auto pixels = gpu.render(renderer);
  EXPECT_EQ(center(pixels), (LinearRGB{4, 2, 1}));
  Detail::OpenGLDisplay display(gpu.device);
  display.begin({64, 64});
  display.hdr(gpu.target.color, 1);
  std::array<unsigned char, 4> pixel{};
  glReadBuffer(GL_BACK);
  glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
  Film film({1, 1});
  const std::array<LinearRGB, 1> sample = {LinearRGB{4, 2, 1}};
  film.addSample(sample);
  const auto expected = film.resolve(1);
  for (int i = 0; i < 3; ++i)
    EXPECT_NEAR(pixel[i], expected.pixels[i], 1);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  e.get<Transform>().rotation = Quaternion::angleAxis({0, 1, 0}, 180);
  EXPECT_EQ(center(gpu.render(renderer)), LinearRGB{});
  e.get<Material>().twoSidedEmission = true;
  EXPECT_EQ(center(gpu.render(renderer)), (LinearRGB{4, 2, 1}));
}
TEST(Rasterization, AreaIntegralUsesPdfAndSampleBudgetDoesNotMultiplyEnergy)
{
  GPU gpu;
  gpu.camera.get<Camera>().projection = Projection::Orthographic;
  gpu.camera.get<Camera>().viewHeight = 1;
  gpu.add(quad(4, 0), Materials::Diffuse({.7f, .7f, .7f}));
  gpu.add(quad(.4f, 2, false), Materials::Emissive({8, 8, 8}), {1.5f, 0, 0});
  auto c = quick();
  c.shadows = false;
  c.reflections = false;
  Rasterization a(c);
  const auto x = center(gpu.render(a));
  c.areaSamples = 32;
  Rasterization b(c);
  const auto y = center(gpu.render(b));
  // Independent midpoint integration over the square light, including emitter and receiver cosine.
  double expected = 0;
  const double px = 1.0 / 128;
  for (int iy = 0; iy < 128; ++iy)
    for (int ix = 0; ix < 128; ++ix) {
      const double dx = 1.5 - .4 + .8 * (ix + .5) / 128 - px, dy = -.4 + .8 * (iy + .5) / 128 - px,
                   r2 = dx * dx + dy * dy + 4;
      expected += 8 * .7 / 3.141592653589793 * 4 / (r2 * r2) * .64 / (128 * 128);
    }
  EXPECT_NEAR(x.r, expected, .05 * expected);
  EXPECT_NEAR(y.r, expected, .05 * expected);
  EXPECT_NEAR(x.r, y.r, .05 * expected);
}
TEST(Rasterization, OffCameraOcclusionAndNearestGlassLayerProduceColoredShadows)
{
  GPU gpu;
  gpu.camera.get<Camera>().projection = Projection::Orthographic;
  gpu.camera.get<Camera>().viewHeight = 1;
  gpu.add(quad(4, 0), Materials::Diffuse({.7f, .7f, .7f}));
  gpu.add(quad(.4f, 2, false), Materials::Emissive({8, 8, 8}), {1.5f, 0, 0});
  Rasterization renderer(quick());
  const auto lit = center(gpu.render(renderer));
  ASSERT_GT(lit.r, .01f);
  auto blocker = gpu.add(quad(.45f, 1), Materials::Diffuse(), {.75f, 0, 0});
  const auto blocked = center(gpu.render(renderer));
  EXPECT_LT(blocked.r, lit.r * .05f);
  blocker.get<Material>() = Materials::Glass(0, 1.5f, {1, .1f, .1f});
  const auto tinted = center(gpu.render(renderer));
  EXPECT_GT(tinted.r, lit.r * .6f);
  EXPECT_LT(tinted.g, tinted.r * .2f);
  auto stats = Detail::RasterizationAccess::statistics(renderer);
  blocker.get<Material>().baseColor = {.1f, 1, .1f};
  const auto green = center(gpu.render(renderer));
  EXPECT_GT(green.g, green.r * 5);
  EXPECT_EQ(Detail::RasterizationAccess::statistics(renderer).shadowBuilds, stats.shadowBuilds + 1);
}
TEST(Rasterization, ProbeCapturesSeeOffCameraEmissionAndCameraMotionReusesWorldCaches)
{
  GPU gpu;
  auto metal = gpu.add(MeshData::sphere(1), Materials::Gold());
  gpu.add(MeshData::cube(2), Materials::Emissive({8, 4, 2}), {0, 0, 7});
  Rasterization renderer(quick());
  const auto a = gpu.render(renderer);
  float max = 0;
  for (auto p : a)
    max = std::max(max, p.r);
  EXPECT_GT(max, .1f);
  auto first = Detail::RasterizationAccess::statistics(renderer);
  EXPECT_EQ(first.probeCaptures, 1u);
  gpu.camera.get<Transform>().position.x = .1f;
  gpu.render(renderer);
  auto moved = Detail::RasterizationAccess::statistics(renderer);
  EXPECT_EQ(moved.probeCaptures, first.probeCaptures);
  EXPECT_EQ(moved.emitterBuilds, first.emitterBuilds);
  EXPECT_EQ(moved.shadowBuilds, first.shadowBuilds);
  metal.get<Material>().roughness = .7f;
  gpu.render(renderer);
  auto changed = Detail::RasterizationAccess::statistics(renderer);
  EXPECT_EQ(changed.probeCaptures, first.probeCaptures + 1);
  EXPECT_EQ(changed.shadowBuilds, first.shadowBuilds);
  gpu.camera.get<Camera>().projection = Projection::Orthographic;
  gpu.render(renderer);
}
TEST(Rasterization, IorOneIsTransparentAndRefractionChangesAnOpaqueBackground)
{
  GPU gpu;
  gpu.add(quad(5, -2), Materials::Emissive({2, .1f, .1f}));
  gpu.add(quad(2, -1), Materials::Emissive({.1f, 2, .1f}), {2.5f, 0, 0});
  auto c = quick();
  c.reflections = false;
  Rasterization renderer(c);
  const auto background = gpu.render(renderer);
  auto glass = gpu.add(MeshData::sphere(1), Materials::Glass(0, 1));
  EXPECT_LT(difference(background, gpu.render(renderer)), 1e-5f);
  glass.get<Material>() = Materials::Glass(0, 1.5f);
  const auto refracted = gpu.render(renderer);
  c.refractions = false;
  Rasterization straight(c);
  const auto unshifted = gpu.render(straight);
  EXPECT_GT(difference(refracted, unshifted), .005f);
  gpu.camera.get<Camera>().projection = Projection::Orthographic;
  gpu.render(renderer);
}

TEST(Rasterization, ZoomAndCameraChangesRestoreTheOriginalPixelsWithoutRebuildingWorldCaches)
{
  GPU gpu;
  gpu.add(MeshData::cube(2), Materials::Gold(), {-1.4f, 0, 0});
  gpu.add(MeshData::sphere(1), Materials::Glass(), {1.4f, 0, 0});
  gpu.add(quad(5, -2), Materials::Emissive({2, .5f, .1f}));
  Rasterization renderer(quick());
  const auto original = SceneSnapshot::capture(gpu.scene, gpu.assets);
  const auto before = gpu.render(renderer);
  const auto caches = Detail::RasterizationAccess::statistics(renderer);
  for (float fov : {15.0f, 90.0f, 120.0f, 60.0f}) {
    gpu.camera.get<Camera>().fovDegrees = fov;
    gpu.render(renderer);
  }
  const auto after = gpu.render(renderer);
  EXPECT_LT(difference(before, after), 1e-5f);
  EXPECT_EQ(Detail::RasterizationAccess::statistics(renderer).probeCaptures, caches.probeCaptures);
  EXPECT_EQ(Detail::RasterizationAccess::statistics(renderer).shadowBuilds, caches.shadowBuilds);
  EXPECT_EQ(SceneSnapshot::capture(gpu.scene, gpu.assets), original);
}

TEST(Rasterization, ScreenSpaceReflectionAddsVisibleEmissionWithoutAProbe)
{
  GPU gpu;
  auto mirror = quad(2, 0);
  for (auto &v : mirror.vertices) {
    v.position.z = -v.position.y;
    v.normal = {0, .70710678f, .70710678f};
  }
  gpu.add(std::move(mirror), Materials::Silver(0));
  gpu.add(MeshData::cube(1), Materials::Emissive({4, 1, .1f}), {0, 1.5f, 0});
  auto c = quick();
  c.shadows = false;
  c.maxReflectionProbes = 0;
  Rasterization ssr(c);
  const auto reflected = gpu.render(ssr);
  // Analytic specular at roughness zero is confined to the mirror direction;
  // a visible reflection must cover more pixels than that singular highlight.
  int colored = 0;
  for (int y = 15; y < 32; ++y)
    for (int x = 20; x < 44; ++x)
      colored += reflected[y * 64 + x].r > .05f;
  EXPECT_GT(colored, 8);
  gpu.camera.get<Camera>().projection = Projection::Orthographic;
  gpu.camera.get<Camera>().viewHeight = 5;
  const auto ortho = gpu.render(ssr);
  EXPECT_GT(difference(ortho, std::vector<LinearRGB>(4096)), .01f);
}
TEST(Rasterization, InteriorGlassTotalInternalReflectionIsFiniteAndProbeBudgetIsBounded)
{
  GPU gpu;
  gpu.camera.get<Transform>().position = {0, 0, .8f};
  gpu.add(MeshData::sphere(1), Materials::Glass());
  gpu.add(MeshData::cube(2), Materials::Emissive({2, 1, .5f}), {0, 0, 4});
  auto c = quick();
  c.maxReflectionProbes = 1;
  Rasterization renderer(c);
  const auto pixels = gpu.render(renderer);
  for (auto p : pixels)
    EXPECT_LE(p.maximum(), 2.01f);
  EXPECT_EQ(Detail::RasterizationAccess::statistics(renderer).probeCaptures, 1u);
  gpu.add(MeshData::sphere(.3f), Materials::Gold(), {0, 0, -3});
  gpu.render(renderer);
  EXPECT_EQ(Detail::RasterizationAccess::statistics(renderer).probeCaptures, 2u);
  gpu.camera.get<Transform>().position = {0, 0, -3};
  gpu.render(renderer);
  EXPECT_EQ(Detail::RasterizationAccess::statistics(renderer).probeCaptures, 3u);
}

TEST(Rasterization, MissingVertexNormalsUseFiniteGeometricNormalsForGlassAndShadows)
{
  GPU gpu;
  auto sphere = MeshData::sphere(1);
  for (auto &vertex : sphere.vertices)
    vertex.normal = {};
  gpu.add(std::move(sphere), Materials::Glass());
  auto floor = quad(5, -2);
  for (auto &vertex : floor.vertices)
    vertex.normal = {};
  gpu.add(std::move(floor), Materials::Emissive({2, .5f, .1f}));
  Rasterization renderer(quick());
  const auto pixels = gpu.render(renderer);
  EXPECT_GT(center(pixels).r, .1f);
}
