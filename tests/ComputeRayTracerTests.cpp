#include <Jade/Jade.hpp>
#include "Renderer/Common/Math.hpp"
#include "internal/Renderer/Host/ProgressiveSession.hpp"
#include "internal/Renderer/Host/RenderHost.hpp"
#include "internal/Platform/Window.hpp"
#include "internal/Renderer/Device/OpenGL/OpenGLDevice.hpp"
#include "internal/Renderer/Presentation/OpenGLDisplay.hpp"
#include "internal/Renderer/Debug/DebugDraw.hpp"
#include <gtest/gtest.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <thread>
#include <atomic>

namespace {
using namespace Jade;
using namespace Jade::Renderer;
struct GpuFixture {
  Scene scene;
  Assets assets;
  Entity camera;
  GpuFixture() {
    camera = scene.create<Archetype<Transform, Camera>>();
    camera.get<Transform>().position = {0, 0, 5}; scene.setCamera(camera);
  }
  Entity add(MeshHandle mesh, Vec3 position, Material material = {}) {
    auto entity = scene.create<Archetype<Transform, Geometry, Material>>();
    entity.get<Transform>().position = position;
    entity.get<Geometry>().mesh = mesh; entity.get<Material>() = material;
    return entity;
  }
  SceneSnapshot snapshot() { return SceneSnapshot::capture(scene, assets); }
};
void matchesCpu(ComputeRayTracer &gpu, const RayScene &scene, const CameraFrame &camera,
                ImageSize size, RayOutput shading = RayOutput::Color) {
  Film film(size);
  gpu.render(scene, {camera, size, 0, 91, {}}, film);
  ASSERT_EQ(film.samples(), 1u);
  std::size_t hits = 0;
  for (int y = 0; y < size.height; ++y) for (int x = 0; x < size.width; ++x) {
    const auto ray = camera.generateRay({x + 0.5f, y + 0.5f}, size);
    SurfaceHit hit; LinearRGB expected;
    if (scene.intersect(ray, hit)) {
      ++hits;
      if (shading == RayOutput::Normal) {
        expected = {hit.shadingNormal.x * 0.5f + 0.5f, hit.shadingNormal.y * 0.5f + 0.5f, hit.shadingNormal.z * 0.5f + 0.5f};
      } else {
        const auto &material = scene.material(hit);
        expected = material.baseColor;
        if (material.twoSidedEmission || hit.frontFace) expected += material.emission;
      }
    }
    const auto actual = film.pixels()[static_cast<std::size_t>(y) * size.width + x];
    EXPECT_NEAR(actual.r, expected.r, 0.0005f) << "pixel " << x << ',' << y;
    EXPECT_NEAR(actual.g, expected.g, 0.0005f) << "pixel " << x << ',' << y;
    EXPECT_NEAR(actual.b, expected.b, 0.0005f) << "pixel " << x << ',' << y;
  }
  if (scene.instanceCount()) EXPECT_GT(hits, 0u);
}
RayTracingSettings exact(RayOutput shading = RayOutput::Color) {
  RayTracingSettings config; config.device = RayDevice::GPU; config.mode = RayMode::RayCast; config.jitter = false; config.output = shading; return config;
}
bool waitFor(const std::function<bool()> &predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!predicate()) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}
} // namespace

TEST(ComputeRayTracer, PrimaryVisibilityMatchesCpuWithSharedMeshesAndHierarchyShear) {
  GpuFixture f;
  const auto mesh = f.assets.add(MeshData::cube());
  const auto first = f.add(mesh, {-0.8f, 0, 0}, Materials::Diffuse({0.9f, 0.2f, 0.1f}));
  const auto second = f.add(mesh, {0.8f, 0, -0.8f}, Materials::Gold());
  first.get<Transform>().rotation = Quaternion::angleAxis({0, 1, 0}, 25);
  const auto parent = f.scene.create<Archetype<Transform>>();
  parent.get<Transform>().scale = {1.4f, 0.8f, 1.2f};
  second.get<Transform>().rotation = Quaternion::angleAxis({0, 0, 1}, 31);
  f.scene.setParent(second, parent);
  ComputeRayTracer gpu(exact());
  EXPECT_FALSE(gpu.deviceName().empty());
  const auto snapshot = f.snapshot(); const auto scene = RayScene::build(snapshot);
  matchesCpu(gpu, scene, *snapshot.camera, {73, 49});
  matchesCpu(gpu, scene, *snapshot.camera, {65, 37});
}

TEST(ComputeRayTracer, NormalsMatchCpuForSmoothTrianglesAndNonuniformTransforms) {
  GpuFixture f;
  const auto e = f.add(f.assets.add(MeshData::sphere(1, 24, 12)), {}, Materials::Glass());
  e.get<Transform>().scale = {1.2f, 0.8f, 1.0f};
  e.get<Transform>().rotation = Quaternion::angleAxis({1, 1, 0}, 36);
  ComputeRayTracer gpu(exact(RayOutput::Normal));
  const auto snapshot = f.snapshot();
  matchesCpu(gpu, RayScene::build(snapshot), *snapshot.camera, {67, 43}, RayOutput::Normal);
  auto settings = exact(RayOutput::Normal); settings.device = RayDevice::CPU; settings.mode = RayMode::PathTrace;
  RayTracing cpu(settings);
  Film cpuFilm({67, 43}), gpuFilm(cpuFilm.size());
  const auto scene = RayScene::build(snapshot);
  const RenderRequest request{*snapshot.camera, cpuFilm.size(), 0, 91, {}};
  cpu.render(scene, request, cpuFilm); gpu.render(scene, request, gpuFilm);
  EXPECT_EQ(cpu.imageEncoding(), ImageEncoding::LinearData);
  EXPECT_EQ(gpu.imageEncoding(), ImageEncoding::LinearData);
  for (std::size_t i = 0; i < cpuFilm.pixels().size(); ++i) {
    EXPECT_NEAR(cpuFilm.pixels()[i].r, gpuFilm.pixels()[i].r, .0005f);
    EXPECT_NEAR(cpuFilm.pixels()[i].g, gpuFilm.pixels()[i].g, .0005f);
    EXPECT_NEAR(cpuFilm.pixels()[i].b, gpuFilm.pixels()[i].b, .0005f);
  }
}

TEST(ComputeRayTracer, SceneEditsUpdateVisibilityMaterialsAndEmptyGeometry) {
  GpuFixture f;
  auto object = f.add(f.assets.add(MeshData::cube(2)), {}, Materials::Diffuse({0.3f, 0.5f, 0.8f}));
  ComputeRayTracer gpu(exact()); RaySceneCompiler compiler;
  for (int variant = 0; variant < 5; ++variant) {
    if (variant == 1) object.get<Material>() = Materials::Emissive({2, 3, 4});
    if (variant == 2) object.get<Transform>().position = {0.7f, 0, -1};
    if (variant == 3) object.get<Geometry>().mesh = f.assets.add(MeshData::sphere());
    if (variant == 4) f.scene.destroy(object);
    const auto snapshot = f.snapshot();
    matchesCpu(gpu, compiler.compile(snapshot), *snapshot.camera, {41, 35});
  }
  const auto snapshot = f.snapshot(); Film empty({9, 7});
  gpu.render(RayScene::build(snapshot), {*snapshot.camera, empty.size(), 0, 0, {}}, empty);
  for (auto pixel : empty.pixels()) EXPECT_EQ(pixel, LinearRGB{});
}

TEST(ComputeRayTracer, OrthographicClippingBackFacesAndZeroDirectionComponents) {
  GpuFixture f;
  auto material = Materials::Emissive({2, 1, 0.5f});
  material.baseColor = {};
  f.add(f.assets.add(MeshData::triangle({-8,-8,0}, {8,-8,0}, {0,8,0})), {}, material);
  f.camera.get<Camera>().projection = Projection::Orthographic;
  f.camera.get<Camera>().viewHeight = 2;
  ComputeRayTracer gpu(exact());
  auto snapshot = f.snapshot(); const auto scene = RayScene::build(snapshot);
  matchesCpu(gpu, scene, *snapshot.camera, {17, 15});
  snapshot.camera->position = {0,0,-5}; snapshot.camera->forward = {0,0,1};
  matchesCpu(gpu, scene, *snapshot.camera, {17, 15});
  snapshot.camera->lens.farPlane = 4;
  Film clipped({17,15}); gpu.render(scene, {*snapshot.camera, clipped.size(), 0, 0, {}}, clipped);
  for (auto pixel : clipped.pixels()) EXPECT_EQ(pixel, LinearRGB{});
}

TEST(ComputeRayTracer, SharedTriangleEdgesRemainWatertightAcrossWorkgroups) {
  GpuFixture f;
  const auto mesh = f.assets.add(MeshData::cube(2));
  f.add(mesh, {}, Materials::Diffuse({0.25f, 0.5f, 0.75f}));
  f.camera.get<Camera>().projection = Projection::Orthographic;
  f.camera.get<Camera>().viewHeight = 1.8f;
  ComputeRayTracer gpu(exact());
  const auto snapshot = f.snapshot(); const auto scene = RayScene::build(snapshot);
  matchesCpu(gpu, scene, *snapshot.camera, {63, 63});
  Film image({63,63}); gpu.render(scene, {*snapshot.camera, image.size(), 0, 0, {}}, image);
  for (const auto color : image.pixels()) EXPECT_EQ(color, (LinearRGB{0.25f,0.5f,0.75f}));
}

TEST(ComputeRayTracer, CancellationDeterminismAndRequestValidation) {
  GpuFixture f; f.add(f.assets.add(MeshData::sphere(1)), {}, Materials::Diffuse({0.7f,0.4f,0.2f}));
  const auto snapshot = f.snapshot(); const auto scene = RayScene::build(snapshot);
  ComputeRayTracer gpu; Film a({23,19}), b(a.size());
  RenderRequest request{*snapshot.camera, a.size(), 0, UINT64_C(0x123456789abcdef0), {}};
  std::stop_source stop; stop.request_stop(); request.cancellation = stop.get_token();
  gpu.render(scene, request, a); EXPECT_EQ(a.samples(), 0u);
  request.cancellation = {}; gpu.render(scene, request, a); gpu.render(scene, request, b);
  EXPECT_EQ(a.resolve().pixels, b.resolve().pixels);
  EXPECT_THROW(gpu.render(scene, request, a), std::invalid_argument);
  request.size = {24,19}; EXPECT_THROW(gpu.render(scene, request, b), std::invalid_argument);
}

TEST(ComputeRayTracer, ProgressiveWorkerRestartAndContextBindingRemainSafe) {
  GpuFixture f; f.add(f.assets.add(MeshData::cube()), {}, Materials::Diffuse());
  RayTracingSettings config; config.device = RayDevice::GPU; config.mode = RayMode::RayCast; config.maxSamples = 2;
  ComputeRayTracer gpu(config);
  auto *previousContext = glfwGetCurrentContext();
  Jade::Detail::ProgressiveSession session; session.bind(gpu);
  for (int run = 0; run < 2; ++run) {
    session.start(); session.submit(f.snapshot(), {31, 21});
    ASSERT_TRUE(waitFor([&] { session.rethrowError(); return session.progress().samplesPerPixel == 2; }));
    ASSERT_TRUE(session.image()); EXPECT_EQ(session.image()->size, (ImageSize{31,21}));
    session.stop(); EXPECT_EQ(glfwGetCurrentContext(), previousContext);
    f.camera.get<Transform>().position.x += 0.4f;
  }
}

TEST(ComputeRayTracer, RuntimeSelectsComputeWithoutChangingCpuRendererDefault) {
  EXPECT_EQ(Config{}.rayTracing.mode, RayMode::PathTrace);
  EXPECT_EQ(Config{}.rayTracing.device, RayDevice::CPU);
  EngineConfig config; config.window.visible = false; config.window.width = config.window.height = 32;
  config.logPerformance = false; config.debug.axes = false;
  config.renderer.rayTracing.mode = RayMode::RayCast;
  config.renderer.rayTracing.device = RayDevice::GPU;
  config.renderer.rayTracing.maxSamples = 1;
  Application app(config);
  auto scene = std::make_shared<Scene>();
  auto camera = scene->create<Archetype<Transform, Camera>>(); camera.get<Transform>().position.z = 5; scene->setCamera(camera);
  auto object = scene->create<Archetype<Transform, Geometry, Material>>();
  object.get<Geometry>().mesh = app.runtime().assets().add(MeshData::cube());
  app.runtime().setScene(scene); app.Run({3});
  EXPECT_EQ(app.runtime().renderer().progress().rendererName, "GPU Ray Casting");
  app.runtime().renderer().setWireframe(true); app.Run({2});
  EXPECT_EQ(app.runtime().renderer().progress().rendererName, "GPU Ray Casting + Wireframe");
}

TEST(ComputeRayTracer, ImageAndWireframeRenderTogetherInTheBackBuffer) {
  WindowConfig config; config.visible = false; config.width = config.height = 64;
  Window window(config);
  Jade::Detail::OpenGLDevice device(window, false);
  Jade::Detail::OpenGLDisplay display(device);
  Jade::Detail::DebugDraw draw(device);
  GpuFixture f; f.add(f.assets.add(MeshData::cube(2)), {}, Materials::Diffuse({0.2f,0.4f,0.8f}));
  const auto snapshot = f.snapshot(); ComputeRayTracer gpu(exact());
  Film film({64,64}); gpu.render(RayScene::build(snapshot), {*snapshot.camera, film.size(), 0, 0, {}}, film);
  display.begin(film.size()); display.image(film.resolve()); draw.wireframe(snapshot, film.size());
  std::vector<unsigned char> pixels(64*64*4);
  glReadBuffer(GL_BACK); glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  std::size_t blue = 0, white = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    if (pixels[i + 2] > pixels[i] + 30 && pixels[i + 2] > pixels[i + 1] + 15) ++blue;
    if (pixels[i] > 195 && pixels[i + 1] > 195 && pixels[i + 2] > 195) ++white;
  }
  EXPECT_GT(blue, 100u); EXPECT_GT(white, 10u); EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(GpuPathTracing, DiffuseGgxAndSmoothRoughGlassMatchCpuLightTransport) {
  GpuFixture f;
  const auto cube = f.assets.add(MeshData::cube());
  auto floor = f.add(cube, {0,-1.1f,0}, Materials::Diffuse({.65f,.65f,.65f})); floor.get<Transform>().scale = {5,.2f,5};
  auto wall = f.add(cube, {0,0,-2.1f}, Materials::Diffuse({.6f,.4f,.3f})); wall.get<Transform>().scale = {5,4,.2f};
  auto lamp = f.add(cube, {0,2.2f,0}, Materials::Emissive({6,5,4})); lamp.get<Transform>().scale = {2,.1f,2};
  auto object = f.add(f.assets.add(MeshData::sphere(1,24,12)), {});
  auto config = exact(); config.mode = RayMode::PathTrace; config.maxBounces = 4; config.rrStartBounce = 3;
  ComputeRayTracer gpu(config);
  auto cpuConfig = config; cpuConfig.device = RayDevice::CPU; JobSystem jobs({2}); RayTracing cpu(jobs, cpuConfig);
  auto metal = Materials::Gold(); metal.roughness = .35f;
  auto roughGlass = Materials::Glass(); roughGlass.roughness = .35f;
  const std::array materials{Materials::Diffuse({.7f,.5f,.3f}), Materials::Gold(0), metal, Materials::Glass(), roughGlass};
  RaySceneCompiler compiler;
  for (std::size_t variant = 0; variant < materials.size(); ++variant) {
    SCOPED_TRACE(variant);
    object.get<Material>() = materials[variant];
    const auto snapshot = f.snapshot(); const auto scene = compiler.compile(snapshot);
    Film a({23,19}), b(a.size());
    for (std::uint64_t sample = 0; sample < 32; ++sample) {
      const RenderRequest request{*snapshot.camera,a.size(),sample,UINT64_C(0xf0123456789abcde),{}};
      cpu.render(scene, request, a); gpu.render(scene, request, b);
    }
    double reference = 0, difference = 0;
    for (std::size_t i = 0; i < a.pixels().size(); ++i) {
      const auto x = a.pixels()[i], y = b.pixels()[i];
      EXPECT_TRUE(y.finite());
      reference += x.r + x.g + x.b;
      difference += std::abs(x.r-y.r) + std::abs(x.g-y.g) + std::abs(x.b-y.b);
    }
    EXPECT_GT(reference, 1);
    EXPECT_LT(difference / reference, .025);
  }
}

TEST(GpuPathTracing, DirectLightIsOccludedByShadowGeometry) {
  GpuFixture f;
  f.add(f.assets.add(MeshData::triangle({-20,-20,0},{20,-20,0},{0,20,0})), {}, Materials::Diffuse({1,1,1}));
  auto emission = Materials::Emissive({10,10,10}); emission.twoSidedEmission = true;
  f.add(f.assets.add(MeshData::triangle({-.2f,-.2f,0},{.2f,-.2f,0},{0,.2f,0})), {2,3,2}, emission);
  auto config = exact(); config.mode = RayMode::PathTrace; config.maxBounces = 1; config.rrStartBounce = 99;
  ComputeRayTracer gpu(config);
  auto snapshot = f.snapshot(); Film visible({1,1});
  gpu.render(RayScene::build(snapshot), {*snapshot.camera,visible.size(),0,0,{}}, visible);
  EXPECT_GT(visible.pixels()[0].r, .001f);
  f.add(f.assets.add(MeshData::cube()), {1,1.5f,1}, Materials::Diffuse({0,0,0}));
  snapshot = f.snapshot(); Film shadow({1,1});
  gpu.render(RayScene::build(snapshot), {*snapshot.camera,shadow.size(),0,0,{}}, shadow);
  EXPECT_EQ(shadow.pixels()[0], LinearRGB{});
}

TEST(GpuPathTracing, BounceLimitControlsTransmissionBeyondPrimaryCameraClipping) {
  GpuFixture f;
  f.camera.get<Camera>().farPlane = 5.5f;
  const auto plane = f.assets.add(MeshData::triangle({-20,-20,0},{20,-20,0},{0,20,0}));
  auto glass = Materials::Glass(); glass.ior = 1; glass.baseColor = {.5f,.8f,1};
  f.add(plane, {}, glass); f.add(plane, {0,0,-1}, glass); f.add(plane, {0,0,-2}, Materials::Emissive({4,4,4}));
  const auto snapshot = f.snapshot(); const auto scene = RayScene::build(snapshot);
  for (std::uint32_t bounces = 1; bounces <= 2; ++bounces) {
    auto config = exact(); config.mode = RayMode::PathTrace; config.maxBounces = bounces; config.rrStartBounce = 99;
    ComputeRayTracer gpu(config); Film film({1,1});
    gpu.render(scene, {*snapshot.camera,film.size(),0,0,{}}, film);
    const auto expected = bounces == 1 ? LinearRGB{} : LinearRGB{1,2.56f,4};
    EXPECT_NEAR(film.pixels()[0].r, expected.r, .0001f);
    EXPECT_NEAR(film.pixels()[0].g, expected.g, .0001f);
    EXPECT_NEAR(film.pixels()[0].b, expected.b, .0001f);
  }
}

TEST(GpuPathTracing, MirrorReflectionAndGlassTotalInternalReflectionReachEmitters) {
  const auto run = [](bool internal) {
    GpuFixture f;
    const auto plane = f.assets.add(MeshData::triangle({-50,-50,0},{50,-50,0},{0,50,0}));
    Material material;
    if (internal) {
      material = Materials::Glass(); material.baseColor = {1,1,1};
      f.camera.get<Transform>().position = {0,0,-1};
      Motion::lookAt(f.camera.get<Transform>(), {3,0,0});
    } else { material = Materials::Gold(); material.baseColor = {.8f,.5f,.2f}; material.roughness = 0; }
    f.add(plane, {}, material);
    auto emission = Materials::Emissive({3,3,3}); emission.twoSidedEmission = true;
    f.add(plane, {0,0,internal ? -2.0f : 6.0f}, emission);
    auto config = exact(); config.mode = RayMode::PathTrace; config.maxBounces = 1; config.rrStartBounce = 99;
    ComputeRayTracer gpu(config); Film film({1,1}); const auto snapshot = f.snapshot();
    const auto scene = RayScene::build(snapshot);
    gpu.render(scene, {*snapshot.camera,film.size(),0,0,{}}, film);
    config.device = RayDevice::CPU; RayTracing cpu(config); Film reference(film.size());
    cpu.render(scene, {*snapshot.camera,reference.size(),0,0,{}}, reference);
    EXPECT_NEAR(film.pixels()[0].r, reference.pixels()[0].r, .0001f) << "internal=" << internal;
    return film.pixels()[0];
  };
  const auto mirror = run(false), internal = run(true);
  EXPECT_NEAR(mirror.r, 2.4f, .0001f); EXPECT_NEAR(mirror.g, 1.5f, .0001f); EXPECT_NEAR(mirror.b, .6f, .0001f);
  EXPECT_NEAR(internal.r, 3, .0001f); EXPECT_NEAR(internal.g, 3, .0001f); EXPECT_NEAR(internal.b, 3, .0001f);
}

TEST(GpuPathTracing, PrimarySamplingMatchesCpuFor64BitSeeds) {
  GpuFixture f; f.add(f.assets.add(MeshData::sphere()), {}, Materials::Diffuse({.7f,.5f,.2f}));
  auto config = exact(); config.jitter = true;
  ComputeRayTracer gpu(config); config.device = RayDevice::CPU; RayTracing cpu(config);
  Film a({37,29}), b(a.size()); const auto snapshot = f.snapshot(); const auto scene = RayScene::build(snapshot);
  for (std::uint64_t sample = 0; sample < 8; ++sample) {
    RenderRequest request{*snapshot.camera,a.size(),sample,UINT64_C(0xffff1234abcdffff),{}};
    cpu.render(scene,request,a); gpu.render(scene,request,b);
  }
  for (std::size_t i = 0; i < a.pixels().size(); ++i) {
    EXPECT_NEAR(a.pixels()[i].r,b.pixels()[i].r,.0001f);
    EXPECT_NEAR(a.pixels()[i].g,b.pixels()[i].g,.0001f);
    EXPECT_NEAR(a.pixels()[i].b,b.pixels()[i].b,.0001f);
  }
}

TEST(GpuPathTracing, InvalidRadianceDoesNotCommitAndTheBackendCanRecover) {
  GpuFixture f;
  f.camera.get<Transform>().position = {};
  auto material = Materials::Diffuse({1,1,1}); material.emission = {3e38f,3e38f,3e38f}; material.twoSidedEmission = true;
  const auto object = f.add(f.assets.add(MeshData::cube(10)), {}, material);
  auto config = exact(); config.mode = RayMode::PathTrace; config.maxBounces = 2; config.rrStartBounce = 99;
  ComputeRayTracer gpu(config); Film film({3,3}); auto snapshot = f.snapshot();
  EXPECT_THROW(gpu.render(RayScene::build(snapshot), {*snapshot.camera,film.size(),0,0,{}}, film), std::runtime_error);
  EXPECT_EQ(film.samples(), 0u);
  object.get<Material>().emission = {};
  snapshot = f.snapshot();
  EXPECT_NO_THROW(gpu.render(RayScene::build(snapshot), {*snapshot.camera,film.size(),0,0,{}}, film));
  EXPECT_EQ(film.samples(), 1u);
  for (const auto color : film.pixels()) EXPECT_EQ(color, LinearRGB{});
}

namespace {
std::atomic<int> gpuImageReadbacks = 0;
PFNGLGETTEXIMAGEPROC savedGetTexImage = nullptr;
void GLAD_API_PTR countedGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void* pixels) {
  ++gpuImageReadbacks; savedGetTexImage(target,level,format,type,pixels);
}
struct ReadbackGuard {
  ReadbackGuard() { gpuImageReadbacks = 0; savedGetTexImage = glad_glGetTexImage; glad_glGetTexImage = countedGetTexImage; }
  ~ReadbackGuard() { glad_glGetTexImage = savedGetTexImage; }
};
PFNGLTEXIMAGE2DPROC savedTexImage2D = nullptr;
PFNGLGETTEXIMAGEPROC coverageGetTexImage = nullptr;
std::size_t poisonedImages = 0, coveredPixels = 0, unwrittenPixels = 0;
void GLAD_API_PTR poisonImage(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height,
                            GLint border, GLenum format, GLenum type, const void* pixels) {
  if (internalFormat == GL_RGBA32F && format == GL_RGBA && type == GL_FLOAT && !pixels) {
    std::vector<float> sentinel(static_cast<std::size_t>(width)*height*4,-123.0f);
    ++poisonedImages;
    savedTexImage2D(target,level,internalFormat,width,height,border,format,type,sentinel.data());
  } else savedTexImage2D(target,level,internalFormat,width,height,border,format,type,pixels);
}
void GLAD_API_PTR inspectCoverage(GLenum target, GLint level, GLenum format, GLenum type, void* pixels) {
  coverageGetTexImage(target,level,format,type,pixels);
  if (target != GL_TEXTURE_2D || format != GL_RGBA || type != GL_FLOAT) return;
  GLint width=0,height=0;
  glGetTexLevelParameteriv(target,level,GL_TEXTURE_WIDTH,&width);
  glGetTexLevelParameteriv(target,level,GL_TEXTURE_HEIGHT,&height);
  const auto* values = static_cast<const float*>(pixels);
  for (std::size_t i=0;i<static_cast<std::size_t>(width)*height;++i) {
    ++coveredPixels; unwrittenPixels += values[i*4+3] != 1.0f;
  }
}
struct CoverageGuard {
  CoverageGuard() {
    poisonedImages=coveredPixels=unwrittenPixels=0;
    savedTexImage2D=glad_glTexImage2D; coverageGetTexImage=glad_glGetTexImage;
    glad_glTexImage2D=poisonImage; glad_glGetTexImage=inspectCoverage;
  }
  ~CoverageGuard() { glad_glTexImage2D=savedTexImage2D; glad_glGetTexImage=coverageGetTexImage; }
};
}

TEST(GpuPathTracing, DarkPixelsAreWrittenAndDownwardAreaLightsIlluminateEveryUnoccludedFloorPixel) {
  GpuFixture f;
  f.camera.get<Transform>().position={0,1,0}; Motion::lookAt(f.camera.get<Transform>(),{}, {0,0,1});
  f.camera.get<Camera>().projection=Projection::Orthographic; f.camera.get<Camera>().viewHeight=2;
  auto floor=f.add(f.assets.add(MeshData::cube()),{0,-.05f,0},Materials::Diffuse({.65f,.65f,.65f}));
  floor.get<Transform>().scale={10,.1f,10};
  auto lamp=f.add(f.assets.add(MeshData::cube()),{0,2,0},Materials::Emissive({14,12,10}));
  lamp.get<Transform>().scale={4,.1f,4};
  auto config=exact(); config.mode=RayMode::PathTrace; config.maxBounces=1;
  ComputeRayTracer gpu(config); const ImageSize size{31,21};
  auto snapshot=f.snapshot(); Film original(size);
  {
    CoverageGuard guard;
    gpu.render(RayScene::build(snapshot),{*snapshot.camera,size,0,100,{}},original);
    EXPECT_GE(poisonedImages,1u); EXPECT_EQ(coveredPixels,size.pixels()); EXPECT_EQ(unwrittenPixels,0u);
  }
  std::size_t black=0;
  for(const auto value:original.pixels()) black+=value.maximum()==0;
  EXPECT_GT(black,size.pixels()/4); // Written zero radiance, despite an unobstructed floor.
  MeshData panel;
  for(Vec3 p:{Vec3{-.5f,-.5f,-.5f},Vec3{.5f,-.5f,-.5f},Vec3{.5f,-.5f,.5f},Vec3{-.5f,-.5f,.5f}})
    panel.vertices.push_back({p,{0,-1,0}});
  panel.indices={0,1,2,0,2,3}; lamp.get<Geometry>().mesh=f.assets.add(std::move(panel));
  snapshot=f.snapshot(); const auto scene=RayScene::build(snapshot);
  auto cpuConfig=config;cpuConfig.device=RayDevice::CPU;JobSystem jobs({2});RayTracing cpu(jobs,cpuConfig);
  for (std::uint64_t seed : {100u,200u,300u}) {
    Film actual(size),reference(size);
    gpu.render(scene,{*snapshot.camera,size,0,seed,{}},actual);
    cpu.render(scene,{*snapshot.camera,size,0,seed,{}},reference);
    for(std::size_t i=0;i<size.pixels();++i) {
      EXPECT_GT(actual.pixels()[i].maximum(),0) << "seed=" << seed << " pixel=" << i;
      EXPECT_NEAR(actual.pixels()[i].r,reference.pixels()[i].r,.0001f);
      EXPECT_NEAR(actual.pixels()[i].g,reference.pixels()[i].g,.0001f);
      EXPECT_NEAR(actual.pixels()[i].b,reference.pixels()[i].b,.0001f);
    }
  }
}

TEST(GpuPathTracing, RunningFramesPresentCompleteTexturesEvenWhenTheirBudgetExpires) {
  WindowConfig wc; wc.visible = false; wc.width = wc.height = 64; Window window(wc);
  JobSystem jobs({2}); Config config; config.rayTracing = exact(); config.rayTracing.maxSamples = 64;
  config.rayTracing.minSamplesPerFrame = 1;
  Jade::Detail::RenderHost host(window,jobs,"Complete GPU frames",config);
  GpuFixture f;
  const auto object = f.add(f.assets.add(MeshData::cube(3)),{},Materials::Diffuse({1,0,0}));
  auto cpuSettings = config.rayTracing; cpuSettings.device = RayDevice::CPU;
  RayTracing cpu(jobs,cpuSettings);
  const ImageSize size{31,21};
  const auto expired = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  host.start(true);
  std::uint64_t previousRevision = 0;
  {
    ReadbackGuard guard;
    for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
      object.get<Material>().baseColor = frameIndex == 1 ? LinearRGB{0,1,0} : LinearRGB{1,0,0};
      const auto snapshot = f.snapshot();
      host.draw(&snapshot,size,RenderMode::RayTracing,false,false,nullptr,false,expired);
      EXPECT_EQ(gpuImageReadbacks,0);
      EXPECT_EQ(host.progress().samplesPerPixel,1u);
      EXPECT_FALSE(host.progress().inFlight);
      EXPECT_GT(host.progress().revision,previousRevision); previousRevision = host.progress().revision;
      const auto frame = host.gpuFrame(); ASSERT_TRUE(frame);
      glWaitSync(frame->ready,0,GL_TIMEOUT_IGNORED); glBindTexture(GL_TEXTURE_2D,frame->texture);
      std::vector<float> hdr(size.pixels()*4);
      // Explicit test capture bypasses the interceptor; runtime must still read nothing.
      savedGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,hdr.data());
      Film reference(size); const auto scene = RayScene::build(snapshot);
      cpu.render(scene,{*snapshot.camera,size,0,0,{}},reference);
      for (std::size_t pixel = 0; pixel < size.pixels(); ++pixel) {
        EXPECT_NEAR(hdr[pixel*4],reference.pixels()[pixel].r,.0001f) << "frame=" << frameIndex << " pixel=" << pixel;
        EXPECT_NEAR(hdr[pixel*4+1],reference.pixels()[pixel].g,.0001f);
        EXPECT_NEAR(hdr[pixel*4+2],reference.pixels()[pixel].b,.0001f);
        EXPECT_FLOAT_EQ(hdr[pixel*4+3],1);
      }
    }
  }
  EXPECT_EQ(glGetError(),GL_NO_ERROR); host.stop();
}

TEST(GpuPathTracing, RuntimeAccumulatesAndPresentsSharedHdrTexturesWithoutImageReadback) {
  WindowConfig wc; wc.visible = false; wc.width = wc.height = 64; Window window(wc);
  JobSystem jobs({2}); Config config;
  config.rayTracing.device = RayDevice::GPU; config.rayTracing.maxSamples = 8;
  Jade::Detail::OpenGLDevice device(window,false); Jade::Detail::OpenGLDisplay display(device);
  Jade::Detail::RenderHost host(window,jobs,"GPU pipeline test",config);
  GpuFixture f; f.add(f.assets.add(MeshData::cube(2)), {}, Materials::Emissive({4,2,1}));
  const auto snapshot = f.snapshot();
  {
    ReadbackGuard guard;
    host.start(true); host.draw(&snapshot,{32,24},RenderMode::RayTracing,false,false);
    ASSERT_TRUE(waitFor([&] { host.rethrowError(); return host.progress().samplesPerPixel == 8; }));
    host.draw(&snapshot,{32,24},RenderMode::RayTracing,false,false);
    EXPECT_EQ(gpuImageReadbacks,0);
  }
  const auto frame = host.gpuFrame(); ASSERT_TRUE(frame);
  EXPECT_EQ(host.progress().rendererName,"GPU Path Tracing");
  glWaitSync(frame->ready,0,GL_TIMEOUT_IGNORED); glBindTexture(GL_TEXTURE_2D,frame->texture);
  std::vector<float> hdr(32*24*4); glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,hdr.data());
  auto cpuSettings = config.rayTracing; cpuSettings.device = RayDevice::CPU;
  RayTracing cpu(jobs,cpuSettings); Film reference({32,24}); const auto scene = RayScene::build(snapshot);
  for (std::uint64_t sample = 0; sample < 8; ++sample) cpu.render(scene,{*snapshot.camera,reference.size(),sample,0,{}},reference);
  for (std::size_t i = 0; i < reference.pixels().size(); ++i) {
    EXPECT_NEAR(hdr[i*4],reference.pixels()[i].r,.0001f);
    EXPECT_NEAR(hdr[i*4+1],reference.pixels()[i].g,.0001f);
    EXPECT_NEAR(hdr[i*4+2],reference.pixels()[i].b,.0001f);
  }
  const auto revision = host.progress().revision;
  auto settings = host.rayTracingSettings(); settings.exposureEV = 1;
  host.setRayTracingSettings(settings);
  display.begin({32,24}); display.image(frame,settings.exposureEV,ImageEncoding::Radiance);
  std::vector<std::uint8_t> bytes(32*24*4); glReadBuffer(GL_BACK); glReadPixels(0,0,32,24,GL_RGBA,GL_UNSIGNED_BYTE,bytes.data());
  const auto expected = reference.resolve(1);
  for (int y = 0; y < 24; ++y) for (int x = 0; x < 32; ++x) for (int c = 0; c < 4; ++c)
    EXPECT_NEAR(bytes[(y*32+x)*4+c],expected.pixels[((23-y)*32+x)*4+c],1);
  EXPECT_EQ(host.progress().samplesPerPixel,8u); EXPECT_EQ(host.progress().revision,revision);
  settings.output = RayOutput::Normal; settings.resolutionScale = .5f; settings.maxSamples = 2;
  host.setRayTracingSettings(settings); host.draw(&snapshot,{32,24},RenderMode::RayTracing,false,false);
  ASSERT_TRUE(waitFor([&] { host.rethrowError(); return host.progress().samplesPerPixel == 2; }));
  EXPECT_EQ(host.progress().size,(ImageSize{16,12}));
  // A pinned old frame survives resize/restarts and cannot be overwritten.
  glBindTexture(GL_TEXTURE_2D,frame->texture); std::vector<float> retained(hdr.size());
  glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,retained.data()); EXPECT_EQ(retained,hdr);
  EXPECT_EQ(glGetError(),GL_NO_ERROR);
  host.stop();
}
