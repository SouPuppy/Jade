#include <Jade/Jade.hpp>
#include "TestScene.hpp"
#include <gtest/gtest.h>
#include <GLFW/glfw3.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace {
using namespace Jade;
using namespace Jade::Renderer;
ApplicationConfig hidden(bool debug = false)
{
  ApplicationConfig config;
  config.window.visible = false;
  config.window.width = config.window.height = 64;
  config.debug = debug;
  config.axisGizmo = false;
  config.logPerformance = false;
  config.frameLimits = 240;
  config.rayTracing.resolutionScale = .5f;
  config.rayTracing.workerCount = 2;
  config.rayTracing.maxSamples = 1;
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

TEST(RenderMode, ITogglesWithDebugDisabledAndHonorsModifiersRepeatsAndEventConsumption)
{
  Application app(hidden());
  MakeTestScene(app);
  EXPECT_FALSE(app.wireframeEnabled());
  tap(GLFW_KEY_I, GLFW_MOD_CONTROL);
  key(GLFW_KEY_I, GLFW_REPEAT);
  app.Run({1});
  EXPECT_FALSE(app.wireframeEnabled());
  auto consumed = app.on<KeyPressed<"I">>([] { return EventFlow::Stop; });
  tap(GLFW_KEY_I);
  app.Run({1});
  EXPECT_FALSE(app.wireframeEnabled());
  consumed.disconnect();
  tap(GLFW_KEY_I);
  app.Run({2});
  EXPECT_TRUE(app.wireframeEnabled());
  EXPECT_EQ(app.renderProgress().rendererName, "Wireframe");
  EXPECT_FALSE(app.renderProgress().samplesPerPixel);
  EXPECT_EQ(std::string_view(glfwGetWindowTitle(glfwGetCurrentContext())), "Jade | Wireframe");
  tap(GLFW_KEY_I);
  app.Run({2});
  EXPECT_FALSE(app.wireframeEnabled());
  EXPECT_EQ(app.renderProgress().rendererName, "RayTracing");
}

TEST(RenderMode, FRetainsTheDebugWireframeShortcut)
{
  Application app(hidden(true));
  MakeTestScene(app);
  tap(GLFW_KEY_F);
  app.Run({1});
  EXPECT_TRUE(app.wireframeEnabled());
  tap(GLFW_KEY_I);
  app.Run({1});
  EXPECT_FALSE(app.wireframeEnabled());
  tap(GLFW_KEY_F, GLFW_MOD_SHIFT);
  app.Run({1});
  EXPECT_FALSE(app.wireframeEnabled());
}

TEST(RenderMode, StartingInWireframeRunsNoIntegratorAndResumingStartsFreshSamples)
{
  Application app(hidden());
  std::atomic<int> calls = 0;
  app.rayTracing().setIntegrator(std::make_unique<CountingIntegrator>(calls));
  auto scene = MakeTestScene(app);
  auto object = scene->create();
  object.add<Transform>();
  object.add<Geometry>(app.assets().add(MeshData::cube()));
  object.add<Material>();
  app.setWireframe(true);
  app.Run({8});
  EXPECT_EQ(calls, 0);
  tap(GLFW_KEY_I);
  app.Run({8});
  EXPECT_GT(calls, 0);
  EXPECT_EQ(app.renderProgress().samplesPerPixel, 1u);
  const auto pausedCalls = calls.load();
  app.setWireframe(true);
  object.get<Transform>().position.x = 1;
  app.Run({8});
  EXPECT_EQ(calls, pausedCalls);
  tap(GLFW_KEY_I);
  app.Run({8});
  EXPECT_GT(calls, pausedCalls);
  EXPECT_EQ(app.renderProgress().samplesPerPixel, 1u);
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
  app.rayTracing().setIntegrator(std::make_unique<Broken>(called));
  MakeTestScene(app);
  auto synchronization = app.on<FrameUpdate>([&](const auto &frame) {
    if (frame.frame == 2)
      EXPECT_TRUE(waitUntil([&] { return called && !app.renderProgress().inFlight; }));
  });
  EXPECT_THROW(app.Run({8}), std::runtime_error);
  synchronization.disconnect();
  EXPECT_NO_THROW(app.rayTracing().setIntegrator(std::make_unique<PathIntegrator>()));
  EXPECT_NO_THROW(app.Run({8}));
}

TEST(RenderMode, RealPathTracingAndWheelInputSurviveRepeatedModeChanges)
{
  Application app(hidden(true));
  auto scene = MakeTestScene(app);
  scene->use<Module::FreeCameraControl>();
  auto object = scene->create();
  object.add<Geometry>(app.assets().add(MeshData::cube()));
  object.add<Material>(Materials::Gold());
  auto lamp = scene->create();
  lamp.add<Transform>().position = {0, 3, 0};
  lamp.add<Geometry>(app.assets().add(MeshData::cube()));
  lamp.add<Material>(Materials::Emissive({4, 3, 2}));
  const auto original = scene->camera().get<Camera>();
  bool traced = false, restored = false;
  std::uint64_t firstRevision = 0;
  app.on<FrameUpdate>([&](const auto &frame) {
    if (frame.frame == 8) {
      traced = waitUntil([&] { return app.renderProgress().samplesPerPixel == 1; });
      firstRevision = app.renderProgress().revision;
      tap(GLFW_KEY_I);
    }
    if (frame.frame == 12) {
      EXPECT_TRUE(app.wireframeEnabled());
      EXPECT_FALSE(app.renderProgress().inFlight);
      EXPECT_EQ(app.renderProgress().rendererName, "Wireframe");
      tap(GLFW_KEY_I);
    }
    if (frame.frame == 20) {
      EXPECT_FALSE(app.wireframeEnabled());
      EXPECT_TRUE(waitUntil([&] { return app.renderProgress().samplesPerPixel == 1; }));
      EXPECT_GT(app.renderProgress().revision, firstRevision);
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
