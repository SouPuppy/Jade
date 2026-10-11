#include <Jade/Jade.hpp>
#include "TestScene.hpp"
#include "internal/Platform/Window.hpp"
#include "internal/Renderer/Device/OpenGL/OpenGLDevice.hpp"
#include "internal/Renderer/Presentation/OpenGLDisplay.hpp"
#include "internal/Renderer/Debug/DebugDraw.hpp"
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

TEST(RenderMode, F1AndIIndependentlyToggleAllFourStatesAndHonorInputConsumption)
{
  Application app(hidden());
  const auto scene = MakeTestScene(app);
  auto& renderer = app.runtime().renderer();
  EXPECT_TRUE(renderer.rayTracingEnabled());
  EXPECT_FALSE(renderer.wireframeEnabled());
  tap(GLFW_KEY_I, GLFW_MOD_CONTROL);
  tap(GLFW_KEY_F1, GLFW_MOD_SHIFT);
  key(GLFW_KEY_I, GLFW_REPEAT);
  key(GLFW_KEY_F1, GLFW_REPEAT);
  auto consumedI = app.on<KeyPressed<"I">>([] { return EventFlow::Stop; });
  auto consumedF = scene->on<KeyPressed<"F1">>([] { return EventFlow::Stop; });
  tap(GLFW_KEY_I); tap(GLFW_KEY_F1);
  app.Run({1});
  EXPECT_TRUE(renderer.rayTracingEnabled());
  EXPECT_FALSE(renderer.wireframeEnabled());
  consumedI.disconnect(); consumedF.disconnect();

  // The former F binding is available for application-defined controls.
  tap(GLFW_KEY_F);
  app.Run({1});
  EXPECT_FALSE(renderer.wireframeEnabled());

  tap(GLFW_KEY_F1);
  app.Run({2});
  EXPECT_TRUE(renderer.rayTracingEnabled());
  EXPECT_TRUE(renderer.wireframeEnabled());
  EXPECT_EQ(renderer.progress().rendererName, "RayTracing + Wireframe");
  EXPECT_EQ(std::string_view(glfwGetWindowTitle(glfwGetCurrentContext())), "Jade | RayTracing + Wireframe");
  tap(GLFW_KEY_I);
  app.Run({2});
  EXPECT_FALSE(renderer.rayTracingEnabled());
  EXPECT_TRUE(renderer.wireframeEnabled());
  EXPECT_EQ(renderer.progress().rendererName, "Wireframe");
  EXPECT_FALSE(renderer.progress().samplesPerPixel);
  tap(GLFW_KEY_F1);
  app.Run({2});
  EXPECT_FALSE(renderer.rayTracingEnabled());
  EXPECT_FALSE(renderer.wireframeEnabled());
  EXPECT_EQ(renderer.progress().rendererName, "Disabled");
  EXPECT_EQ(std::string_view(glfwGetWindowTitle(glfwGetCurrentContext())), "Jade | Disabled");
  tap(GLFW_KEY_I);
  app.Run({2});
  EXPECT_TRUE(renderer.rayTracingEnabled());
  EXPECT_FALSE(renderer.wireframeEnabled());
  EXPECT_EQ(renderer.progress().rendererName, "RayTracing");
}

TEST(RenderMode, RunningFramesAreIndependentAndPEnablesPausedAccumulation)
{
  for (const auto device : {RayDevice::CPU, RayDevice::GPU}) {
    auto config = hidden(); config.renderer.rayTracing.device = device;
    config.renderer.rayTracing.maxSamples = 3;
    Application app(config); MakeTestScene(app);
    auto& renderer = app.runtime().renderer();
    std::uint64_t previousRevision = 0, pausedRevision = 0;
    auto consumed = app.on<KeyPressed<"P">>([] { return EventFlow::Stop; });
    tap(GLFW_KEY_P); app.Run({1}); EXPECT_FALSE(app.runtime().paused());
    consumed.disconnect();
    tap(GLFW_KEY_P, GLFW_MOD_CONTROL); key(GLFW_KEY_P, GLFW_REPEAT);
    app.on<FrameUpdate>([&](const auto& frame) {
      if (frame.frame == 1) EXPECT_FALSE(app.runtime().paused());
      if (frame.frame == 2 || frame.frame == 3) {
        const auto progress = renderer.progress();
        EXPECT_TRUE(progress.samplesPerPixel.has_value());
        EXPECT_GE(*progress.samplesPerPixel, 1u);
        EXPECT_LE(*progress.samplesPerPixel, 3u);
        EXPECT_FALSE(progress.inFlight);
        EXPECT_GT(progress.revision, previousRevision);
        previousRevision = progress.revision;
      }
      if (frame.frame == 3) tap(GLFW_KEY_P);
      if (frame.frame == 4) EXPECT_TRUE(app.runtime().paused());
      if (frame.frame == 5) {
        ASSERT_TRUE(waitUntil([&] { return renderer.progress().samplesPerPixel == 3; }));
        pausedRevision = renderer.progress().revision;
      }
      if (frame.frame == 6) {
        EXPECT_EQ(renderer.progress().revision, pausedRevision);
        tap(GLFW_KEY_P);
      }
      if (frame.frame == 7) EXPECT_FALSE(app.runtime().paused());
      if (frame.frame == 8) {
        EXPECT_GT(renderer.progress().revision, pausedRevision);
        EXPECT_FALSE(renderer.progress().inFlight);
      }
    });
    app.Run({8});
  }
}

TEST(RenderMode, WireframeOverlaysTheImageWithoutRestartingPathTracing)
{
  Application app(hidden());
  app.runtime().setPaused(true);
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
      tap(GLFW_KEY_F1);
    }
    if (frame.frame == 5) {
      EXPECT_TRUE(renderer.rayTracingEnabled());
      EXPECT_TRUE(renderer.wireframeEnabled());
      EXPECT_EQ(renderer.progress().revision, revision);
      EXPECT_EQ(renderer.progress().samplesPerPixel, 1u);
      EXPECT_EQ(calls, completedCalls);
      tap(GLFW_KEY_F1);
    }
    if (frame.frame == 7) {
      EXPECT_FALSE(renderer.wireframeEnabled());
      EXPECT_TRUE(renderer.rayTracingEnabled());
      EXPECT_EQ(renderer.progress().revision, revision);
      EXPECT_EQ(calls, completedCalls);
    }
  });
  app.Run({8});
  EXPECT_GT(completedCalls, 0);

  // Hidden-window front buffers after swap are not a reliable capture surface.
  // Verify the presentation passes before swapping, with the same scene/image.
  WindowConfig windowConfig; windowConfig.visible = false; windowConfig.width = windowConfig.height = 64;
  Window window(windowConfig);
  Jade::Detail::OpenGLDevice device(window, false);
  Jade::Detail::OpenGLDisplay display(device);
  Jade::Detail::DebugDraw debug(device);
  Film film({64, 64});
  film.addSample(std::vector<LinearRGB>(film.size().pixels(), LinearRGB{1, 0, 0}));
  display.begin(film.size());
  display.image(film.resolve());
  debug.wireframe(SceneSnapshot::capture(*scene, app.runtime().assets()), film.size());
  std::array<unsigned char, 64 * 64 * 4> pixels{};
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  unsigned red = 0, white = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    if (pixels[i] > 100 && pixels[i + 1] < 10 && pixels[i + 2] < 10) ++red;
    if (pixels[i] > 100 && pixels[i + 1] > 100 && pixels[i + 2] > 100) ++white;
  }
  EXPECT_GT(red, 1000u);
  EXPECT_GT(white, 10u);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(RenderMode, UnifiedRaySettingsSwitchAlgorithmsAndOutputsDuringRun)
{
  Application app(hidden());
  app.runtime().setPaused(true);
  auto scene = MakeTestScene(app);
  auto object = scene->create<Archetype<Transform, Geometry, Material>>();
  object.get<Geometry>().mesh = app.runtime().assets().add(MeshData::cube());
  auto& renderer = app.runtime().renderer();
  std::atomic<int> calls = 0;
  renderer.rayTracing().setIntegrator(std::make_unique<CountingIntegrator>(calls));
  int initialCalls = 0;
  ImageSize quarterSize;
  app.on<FrameUpdate>([&](const auto& frame) {
    if (frame.frame < 2 || frame.frame > 6) return;
    ASSERT_TRUE(waitUntil([&] { return renderer.progress().samplesPerPixel >= 1; }));
    auto settings = renderer.rayTracingSettings();
    if (frame.frame == 2) {
      initialCalls = calls;
      int width = 0, height = 0;
      glfwGetFramebufferSize(glfwGetCurrentContext(), &width, &height);
      quarterSize = {static_cast<int>(std::lround(width * .25)), static_cast<int>(std::lround(height * .25))};
      settings.device = RayDevice::GPU;
      renderer.setRayTracingSettings(settings);
      EXPECT_FALSE(renderer.progress().samplesPerPixel.has_value());
    } else if (frame.frame == 3) {
      EXPECT_EQ(renderer.progress().rendererName, "GPU Path Tracing");
      EXPECT_EQ(calls, initialCalls);
      settings.output = RayOutput::Normal;
      settings.resolutionScale = .25f;
      renderer.setRayTracingSettings(settings);
    } else if (frame.frame == 4) {
      EXPECT_EQ(renderer.progress().size, quarterSize);
      EXPECT_EQ(renderer.rayTracingSettings().output, RayOutput::Normal);
      settings.mode = RayMode::PathTrace;
      settings.device = RayDevice::CPU;
      renderer.setRayTracingSettings(settings);
    } else if (frame.frame == 5) {
      EXPECT_EQ(renderer.progress().rendererName, "RayTracing");
      EXPECT_EQ(calls, initialCalls); // Normal output bypasses the transport integrator.
      settings.output = RayOutput::Color;
      settings.maxSamples = 2;
      renderer.setRayTracingSettings(settings);
    } else if (frame.frame == 6) {
      ASSERT_TRUE(waitUntil([&] { return renderer.progress().samplesPerPixel == 2; }));
      EXPECT_GT(calls, initialCalls);
      const auto completed = renderer.progress();
      auto invalid = settings;
      invalid.mode = static_cast<RayMode>(99);
      EXPECT_THROW(renderer.setRayTracingSettings(invalid), std::invalid_argument);
      EXPECT_EQ(renderer.rayTracingSettings(), settings);
      EXPECT_EQ(renderer.progress().revision, completed.revision);
      settings.exposureEV = 2;
      renderer.setRayTracingSettings(settings);
      EXPECT_EQ(renderer.progress().samplesPerPixel, 2u);
      EXPECT_EQ(renderer.progress().revision, completed.revision);
    }
  });
  app.Run({8});
  EXPECT_GT(initialCalls, 0);
}

TEST(RenderMode, DisabledRenderingStartsNoIntegratorAndResumingStartsFreshSamples)
{
  auto config = hidden();
  config.renderer.rayTracing.enabled = false;
  config.renderer.wireframe = true;
  Application app(config);
  std::atomic<int> calls = 0;
  auto& renderer = app.runtime().renderer();
  renderer.rayTracing().setIntegrator(std::make_unique<CountingIntegrator>(calls));
  auto scene = MakeTestScene(app);
  auto object = scene->create<Archetype<Transform, Geometry, Material>>();
  object.get<Geometry>().mesh = app.runtime().assets().add(MeshData::cube());
  // Await sampling while Run owns the coordinator; eight frames alone do not
  // guarantee worker completion on a busy machine.
  app.on<FrameUpdate>([&](const auto& frame) {
    if (frame.frame == 3 && renderer.rayTracingEnabled())
      ASSERT_TRUE(waitUntil([&] { return renderer.progress().samplesPerPixel == 1; }));
  });
  app.Run({8});
  EXPECT_EQ(calls, 0);
  tap(GLFW_KEY_I);
  app.Run({8});
  EXPECT_GT(calls, 0);
  EXPECT_TRUE(renderer.wireframeEnabled());
  EXPECT_EQ(renderer.progress().samplesPerPixel, 1u);
  const auto completedCalls = calls.load();
  renderer.setRayTracingEnabled(false);
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
  config.renderer.rayTracing.enabled = false;
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
  EXPECT_FALSE(app.runtime().renderer().rayTracingEnabled());
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
      EXPECT_FALSE(app.runtime().renderer().rayTracingEnabled());
      EXPECT_FALSE(app.runtime().renderer().progress().inFlight);
      EXPECT_EQ(app.runtime().renderer().progress().rendererName, "Disabled");
      tap(GLFW_KEY_I);
    }
    if (frame.frame == 20) {
      EXPECT_TRUE(app.runtime().renderer().rayTracingEnabled());
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
