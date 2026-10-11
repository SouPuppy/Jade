#include <Jade/Jade.hpp>
#include "TestScene.hpp"
#include <gtest/gtest.h>
#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace {
using namespace Jade;
using namespace Jade::Renderer;
EngineConfig hidden(bool debug = false)
{
  EngineConfig config;
  config.window.visible = false;
  config.window.width = config.window.height = 64;
  config.debug.shortcuts = debug;
  config.debug.axes = false;
  config.logPerformance = false;
  config.frameLimits = 240;
  config.renderer.rayTracing.resolutionScale = .5f;
  config.jobs.workerCount = 2;
  config.renderer.rayTracing.maxSamples = 1;
  return config;
}
void key(int code, int action, int modifiers = 0)
{
  auto window = glfwGetCurrentContext();
  auto focus = glfwSetWindowFocusCallback(window, nullptr);
  glfwSetWindowFocusCallback(window, focus);
  focus(window, GLFW_TRUE);
  auto callback = glfwSetKeyCallback(window, nullptr);
  glfwSetKeyCallback(window, callback);
  callback(window, code, 0, action, modifiers);
}
void tap(int code, int modifiers = 0)
{
  key(code, GLFW_PRESS, modifiers);
  key(code, GLFW_RELEASE, modifiers);
}
template <class Predicate> bool waitUntil(Predicate predicate)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!predicate() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return predicate();
}
class CountingIntegrator final : public Integrator {
public:
  explicit CountingIntegrator(std::atomic<int> &calls) : calls(calls) {}
  LinearRGB Li(const Ray &, const RayScene &, Sampler &) const override
  {
    ++calls;
    return {1, 0, 0};
  }
  std::atomic<int> &calls;
};
} // namespace

TEST(RenderMode, FAndIIndependentlyToggleAllFourStatesAndHonorInputConsumption)
{
  Application app(hidden());
  const auto scene = MakeTestScene(app);
  auto& renderer = app.runtime().renderer();
  EXPECT_TRUE(renderer.renderEnabled());
  EXPECT_FALSE(renderer.wireframeEnabled());
  tap(GLFW_KEY_I, GLFW_MOD_CONTROL);
  tap(GLFW_KEY_F, GLFW_MOD_SHIFT);
  key(GLFW_KEY_I, GLFW_REPEAT);
  key(GLFW_KEY_F, GLFW_REPEAT);
  auto consumedI = app.on<KeyPressed<"I">>([] { return EventFlow::Stop; });
  auto consumedF = scene->on<KeyPressed<"F">>([] { return EventFlow::Stop; });
  tap(GLFW_KEY_I); tap(GLFW_KEY_F);
  app.Run({1});
  EXPECT_TRUE(renderer.renderEnabled());
  EXPECT_FALSE(renderer.wireframeEnabled());
  consumedI.disconnect(); consumedF.disconnect();

  tap(GLFW_KEY_F);
  app.Run({2});
  EXPECT_TRUE(renderer.renderEnabled());
  EXPECT_TRUE(renderer.wireframeEnabled());
  EXPECT_EQ(renderer.progress().rendererName, "RayTracing + Wireframe");
  EXPECT_EQ(std::string_view(glfwGetWindowTitle(glfwGetCurrentContext())), "Jade | RayTracing + Wireframe");
  tap(GLFW_KEY_I);
  app.Run({2});
  EXPECT_FALSE(renderer.renderEnabled());
  EXPECT_TRUE(renderer.wireframeEnabled());
  EXPECT_EQ(renderer.progress().rendererName, "Wireframe");
  EXPECT_FALSE(renderer.progress().samplesPerPixel);
  tap(GLFW_KEY_F);
  app.Run({2});
  EXPECT_FALSE(renderer.renderEnabled());
  EXPECT_FALSE(renderer.wireframeEnabled());
  EXPECT_EQ(renderer.progress().rendererName, "Disabled");
  EXPECT_EQ(std::string_view(glfwGetWindowTitle(glfwGetCurrentContext())), "Jade | Disabled");
  tap(GLFW_KEY_I);
  app.Run({2});
  EXPECT_TRUE(renderer.renderEnabled());
  EXPECT_FALSE(renderer.wireframeEnabled());
  EXPECT_EQ(renderer.progress().rendererName, "RayTracing");
}

TEST(RenderMode, WireframeOverlaysTheImageWithoutRestartingPathTracing)
{
  Application app(hidden());
  std::atomic<int> calls = 0;
  auto& renderer = app.runtime().renderer();
  renderer.rayTracing().setIntegrator(std::make_unique<CountingIntegrator>(calls));
  auto scene = MakeTestScene(app);
  auto object = scene->create<Archetype<Transform, Geometry, Material>>();
  object.get<Geometry>().mesh = app.runtime().assets().add(MeshData::cube(2));
  std::uint64_t revision = 0;
  int completedCalls = 0;
  app.on<FrameUpdate>([&](const auto& frame) {
    if (frame.frame == 3) {
      ASSERT_TRUE(waitUntil([&] { return renderer.progress().samplesPerPixel == 1; }));
      revision = renderer.progress().revision;
      completedCalls = calls;
      tap(GLFW_KEY_F);
    }
    if (frame.frame == 5) {
      EXPECT_TRUE(renderer.renderEnabled());
      EXPECT_TRUE(renderer.wireframeEnabled());
      EXPECT_EQ(renderer.progress().revision, revision);
      EXPECT_EQ(renderer.progress().samplesPerPixel, 1u);
      EXPECT_EQ(calls, completedCalls);
      std::array<unsigned char, 64 * 64 * 4> pixels;
      glReadBuffer(GL_FRONT);
      glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      unsigned red = 0, white = 0;
      for (std::size_t i = 0; i < pixels.size(); i += 4) {
        if (pixels[i] > 100 && pixels[i + 1] < 10 && pixels[i + 2] < 10) ++red;
        if (pixels[i] > 100 && pixels[i + 1] > 100 && pixels[i + 2] > 100) ++white;
      }
      EXPECT_GT(red, 1000u);
      EXPECT_GT(white, 10u);
      EXPECT_EQ(glGetError(), GL_NO_ERROR);
      tap(GLFW_KEY_F);
    }
    if (frame.frame == 7) {
      EXPECT_FALSE(renderer.wireframeEnabled());
      EXPECT_TRUE(renderer.renderEnabled());
      EXPECT_EQ(renderer.progress().revision, revision);
      EXPECT_EQ(calls, completedCalls);
    }
  });
  app.Run({8});
  EXPECT_GT(completedCalls, 0);
}

TEST(RenderMode, DisabledRenderingStartsNoIntegratorAndResumingStartsFreshSamples)
{
  auto config = hidden();
  config.renderer.render = false;
  config.renderer.wireframe = true;
  Application app(config);
  std::atomic<int> calls = 0;
  auto& renderer = app.runtime().renderer();
  renderer.rayTracing().setIntegrator(std::make_unique<CountingIntegrator>(calls));
  auto scene = MakeTestScene(app);
  auto object = scene->create<Archetype<Transform, Geometry, Material>>();
  object.get<Geometry>().mesh = app.runtime().assets().add(MeshData::cube());
  app.Run({8});
  EXPECT_EQ(calls, 0);
  tap(GLFW_KEY_I);
  app.Run({8});
  EXPECT_GT(calls, 0);
  EXPECT_TRUE(renderer.wireframeEnabled());
  EXPECT_EQ(renderer.progress().samplesPerPixel, 1u);
  const auto completedCalls = calls.load();
  renderer.setRender(false);
  object.get<Transform>().position.x = 1;
  app.Run({8});
  EXPECT_EQ(calls, completedCalls);
  tap(GLFW_KEY_I);
  app.Run({8});
  EXPECT_GT(calls, completedCalls);
  EXPECT_TRUE(renderer.wireframeEnabled());
  EXPECT_EQ(renderer.progress().samplesPerPixel, 1u);
}

TEST(RenderMode, DisabledGeometryStillSimulatesAndAllowsIndependentDebugOverlays)
{
  auto config = hidden(true);
  config.renderer.render = false;
  config.debug.physics = true;
  config.debug.axes = true;
  Application app(config);
  auto scene = MakeTestScene(app);
  // Invalid render data must remain unused until a scene-rendering switch is enabled.
  auto body = scene->create<Archetype<Transform, Geometry, RigidBody>>();
  body.get<Geometry>().mesh = app.runtime().assets().add(MeshData::cube());
  body.add<Collider>(Box{});
  bool moving = false;
  app.on<FrameUpdate>([&](const auto& frame) {
    if (frame.frame == 6) moving = body.get<RigidBody>().velocity.y < 0;
  });
  EXPECT_NO_THROW(app.Run({8}));
  EXPECT_TRUE(moving);
  EXPECT_TRUE(app.runtime().physicsDebugEnabled());
  EXPECT_FALSE(app.runtime().renderer().renderEnabled());
  EXPECT_FALSE(app.runtime().renderer().wireframeEnabled());
  app.runtime().renderer().setWireframe(true);
  EXPECT_THROW(app.Run({1}), std::invalid_argument);
}


TEST(RenderMode, WorkerFailureReleasesIntegratorLockAndAllowsRecovery)
{
  class Broken final : public Integrator {
  public:
    explicit Broken(std::atomic<bool> &called) : called(called) {}
    LinearRGB Li(const Ray &, const RayScene &, Sampler &) const override
    {
      called = true;
      throw std::runtime_error("Integrator failure");
    }
    std::atomic<bool> &called;
  };
  Application app(hidden());
  std::atomic<bool> called = false;
  app.runtime().renderer().rayTracing().setIntegrator(std::make_unique<Broken>(called));
  MakeTestScene(app);
  auto synchronization = app.on<FrameUpdate>([&](const auto &frame) {
    if (frame.frame == 2)
      EXPECT_TRUE(waitUntil([&] { return called && !app.runtime().renderer().progress().inFlight; }));
  });
  EXPECT_THROW(app.Run({8}), std::runtime_error);
  synchronization.disconnect();
  EXPECT_NO_THROW(app.runtime().renderer().rayTracing().setIntegrator(std::make_unique<PathIntegrator>()));
  EXPECT_NO_THROW(app.Run({8}));
}

TEST(RenderMode, RealPathTracingAndWheelInputSurviveRepeatedRenderToggles)
{
  Application app(hidden(true));
  auto scene = MakeTestScene(app);
  scene->use<Module::FreeCameraControl>();
  auto object = scene->create();
  object.add<Geometry>(app.runtime().assets().add(MeshData::cube()));
  object.add<Material>(Materials::Gold());
  auto lamp = scene->create();
  lamp.add<Transform>().position = {0, 3, 0};
  lamp.add<Geometry>(app.runtime().assets().add(MeshData::cube()));
  lamp.add<Material>(Materials::Emissive({4, 3, 2}));
  const auto original = scene->camera().get<Camera>();
  bool traced = false, restored = false;
  std::uint64_t firstRevision = 0;
  app.on<FrameUpdate>([&](const auto &frame) {
    if (frame.frame == 8) {
      traced = waitUntil([&] { return app.runtime().renderer().progress().samplesPerPixel == 1; });
      firstRevision = app.runtime().renderer().progress().revision;
      tap(GLFW_KEY_I);
    }
    if (frame.frame == 12) {
      EXPECT_FALSE(app.runtime().renderer().renderEnabled());
      EXPECT_FALSE(app.runtime().renderer().progress().inFlight);
      EXPECT_EQ(app.runtime().renderer().progress().rendererName, "Disabled");
      tap(GLFW_KEY_I);
    }
    if (frame.frame == 20) {
      EXPECT_TRUE(app.runtime().renderer().renderEnabled());
      EXPECT_TRUE(waitUntil([&] { return app.runtime().renderer().progress().samplesPerPixel == 1; }));
      EXPECT_GT(app.runtime().renderer().progress().revision, firstRevision);
      auto window = glfwGetCurrentContext();
      auto scroll = glfwSetScrollCallback(window, nullptr);
      glfwSetScrollCallback(window, scroll);
      scroll(window, 0, -80);
    }
    if (frame.frame == 24) {
      EXPECT_LE(scene->camera().get<Camera>().fovDegrees, 120);
      auto window = glfwGetCurrentContext();
      auto scroll = glfwSetScrollCallback(window, nullptr);
      glfwSetScrollCallback(window, scroll);
      scroll(window, 0, 80);
    }
    if (frame.frame == 28) {
      restored = std::abs(scene->camera().get<Camera>().fovDegrees - original.fovDegrees) < 1e-4f;
      EXPECT_NE(std::string_view(glfwGetWindowTitle(glfwGetCurrentContext())).find("RayTracing"),
                std::string_view::npos);
    }
    if (frame.frame > 30 && frame.frame % 30 == 1)
      tap(GLFW_KEY_I);
  });
  app.Run({240});
  EXPECT_TRUE(traced);
  EXPECT_TRUE(restored);
}
