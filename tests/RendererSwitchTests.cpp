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
  ApplicationConfig c;
  c.window.visible = false;
  c.window.width = c.window.height = 64;
  c.debug = debug;
  c.axisGizmo = false;
  c.logPerformance = false;
  c.frameLimits = 240;
  return c;
}
struct Count {
  int frames = 0;
  bool fail = false;
};
class FrameA : public FrameBackend {
public:
  explicit FrameA(Count &c) : c(c) {}
  std::string_view name() const noexcept override { return "FrameA"; }
  void render(const SceneSnapshot &, const FrameRequest &, FrameTarget &target) override
  {
    ++c.frames;
    if (c.fail)
      throw std::runtime_error("Frame failure");
    target.clear({1, 0, 0});
  }
  void configure() { assertConfigurable(); }
  Count &c;
};
class FrameB : public FrameA {
public:
  using FrameA::FrameA;
  std::string_view name() const noexcept override { return "FrameB"; }
};
struct Work {
  std::atomic<int> calls = 0, cancelled = 0;
  std::atomic<bool> block = true;
};
class Slow : public ProgressiveBackend {
public:
  explicit Slow(Work &w) : w(w) {}
  std::string_view name() const noexcept override { return "Slow"; }
  void render(const RayScene &, const RenderRequest &r, Film &film) override
  {
    ++w.calls;
    while (w.block && !r.cancellation.stop_requested())
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (r.cancellation.stop_requested()) {
      ++w.cancelled;
      return;
    }
    std::vector<LinearRGB> pixels(film.size().pixels(), {0, 1, 0});
    film.addSample(pixels);
  }
  void configure() { assertConfigurable(); }
  Work &w;
};
void key(int code, int action, int modifiers = 0)
{
  auto window = glfwGetCurrentContext();
  auto f = glfwSetWindowFocusCallback(window, nullptr);
  glfwSetWindowFocusCallback(window, f);
  f(window, GLFW_TRUE);
  auto k = glfwSetKeyCallback(window, nullptr);
  glfwSetKeyCallback(window, k);
  k(window, code, 0, action, modifiers);
}
void tap(int code, int modifiers = 0)
{
  key(code, GLFW_PRESS, modifiers);
  key(code, GLFW_RELEASE, modifiers);
}
} // namespace
TEST(RendererSwitch, ConcreteRegistryReplacementPreservesOrderAndRejectsUnknownSelection)
{
  Application app(hidden());
  EXPECT_EQ(app.activeRenderer(), nullptr);
  Count a, b, replaced;
  auto &first = app.use<FrameA>(a);
  app.use<FrameB>(b);
  EXPECT_EQ(app.activeRenderer(), &first);
  auto &updated = app.use<FrameA>(replaced);
  EXPECT_EQ(app.activeRenderer(), &updated);
  app.cycleRenderer();
  EXPECT_EQ(app.activeRenderer()->name(), "FrameB");
  app.cycleRenderer();
  EXPECT_EQ(app.activeRenderer(), &updated);
  EXPECT_THROW(app.setRenderer<RayTracing>(), std::invalid_argument);
  MakeTestScene(app);
  app.Run({2});
  EXPECT_EQ(replaced.frames, 2);
  EXPECT_EQ(a.frames, 0);
  EXPECT_FALSE(app.renderProgress().samplesPerPixel);
  // Replacing a borrowed progressive backend must destroy its session before releasing the backend.
  Work work;
  app.use<Slow>(work);
  EXPECT_NO_THROW(app.use<Slow>(work));
}
TEST(RendererSwitch, SelectionIsDeferredToNextFrameAndLastRequestWins)
{
  Application app(hidden());
  Count a, b;
  app.use<FrameA>(a);
  app.use<FrameB>(b);
  MakeTestScene(app);
  app.on<FrameUpdate>([&](const auto &f) {
    if (f.frame == 1) {
      app.setRenderer<FrameB>();
      app.setRenderer<FrameA>();
      app.setRenderer<FrameB>();
      EXPECT_EQ(app.activeRenderer()->name(), "FrameA");
    } else
      EXPECT_EQ(app.activeRenderer()->name(), "FrameB");
  });
  app.Run({3});
  EXPECT_EQ(a.frames, 1);
  EXPECT_EQ(b.frames, 2);
}
TEST(RendererSwitch, IWorksWithDebugDisabledAndHonorsModifiersRepeatsAndEventConsumption)
{
  Application app(hidden());
  Count a, b;
  app.use<FrameA>(a);
  app.use<FrameB>(b);
  MakeTestScene(app);
  tap(GLFW_KEY_I, GLFW_MOD_CONTROL);
  key(GLFW_KEY_I, GLFW_REPEAT);
  app.Run({1});
  EXPECT_EQ(a.frames, 1);
  EXPECT_EQ(b.frames, 0);
  auto consumed = app.on<KeyPressed<"I">>([] { return EventFlow::Stop; });
  tap(GLFW_KEY_I);
  app.Run({2});
  EXPECT_EQ(b.frames, 0);
  consumed.disconnect();
  tap(GLFW_KEY_I);
  app.Run({2});
  EXPECT_EQ(app.activeRenderer()->name(), "FrameB");
  EXPECT_GT(b.frames, 0);
}
TEST(RendererSwitch, SwitchingCancelsWorkAndWireframeSuspendsBothBackends)
{
  Application app(hidden(true));
  Count count;
  Work work;
  app.use<Slow>(work);
  auto &frame = app.use<FrameA>(count);
  auto scene = MakeTestScene(app);
  auto e = scene->create();
  e.add<Geometry>(app.assets().add(MeshData::cube()));
  e.add<Material>();
  bool switched = false;
  app.on<FrameUpdate>([&](const auto &f) {
    if (f.frame > 1 && work.calls && !switched) {
      app.setRenderer<FrameA>();
      switched = true;
    }
  });
  app.Run({12});
  ASSERT_TRUE(switched);
  EXPECT_GT(work.cancelled, 0);
  EXPECT_GT(count.frames, 0);
  EXPECT_EQ(app.activeRenderer(), &frame);
  app.setWireframe(true);
  auto oldFrames = count.frames;
  auto oldCalls = work.calls.load();
  tap(GLFW_KEY_I);
  app.Run({3});
  EXPECT_EQ(app.activeRenderer()->name(), "Slow");
  EXPECT_EQ(count.frames, oldFrames);
  EXPECT_EQ(work.calls, oldCalls);
  EXPECT_TRUE(app.wireframeEnabled());
  work.block = false;
  tap(GLFW_KEY_F);
  app.Run({8});
  EXPECT_FALSE(app.wireframeEnabled());
  EXPECT_GT(work.calls, oldCalls);
}
TEST(RendererSwitch, AllRegisteredBackendsLockDuringRunAndFailureReleasesThem)
{
  Application app(hidden());
  Count a, b;
  Work work;
  auto &x = app.use<FrameA>(a);
  auto &y = app.use<FrameB>(b);
  auto &slow = app.use<Slow>(work);
  MakeTestScene(app);
  auto handler = app.on<FrameUpdate>([&] {
    EXPECT_THROW(x.configure(), std::logic_error);
    EXPECT_THROW(y.configure(), std::logic_error);
    EXPECT_THROW(slow.configure(), std::logic_error);
    EXPECT_THROW(app.use<FrameB>(b), std::logic_error);
  });
  app.Run({1});
  EXPECT_NO_THROW(slow.configure());
  handler.disconnect();
  a.fail = true;
  EXPECT_THROW(app.Run({1}), std::runtime_error);
  EXPECT_NO_THROW(x.configure());
  EXPECT_NO_THROW(y.configure());
  EXPECT_NO_THROW(slow.configure());
  app.setRenderer<FrameB>();
  EXPECT_NO_THROW(app.Run({1}));
}

TEST(RendererSwitch, RealBackendsAndWheelInputSurviveRepeatedModeChanges)
{
  Application app(hidden(true));
  RasterizationConfig raster;
  raster.areaSamples = 4;
  raster.shadowResolution = 32;
  raster.reflectionProbeResolution = 16;
  raster.maxReflectionProbes = 1;
  app.use<Rasterization>(raster);
  RayTracingConfig ray;
  ray.resolutionScale = .5f;
  ray.workerCount = 2;
  ray.maxSamples = 1;
  app.use<RayTracing>(ray);
  auto scene = MakeTestScene(app);
  scene->use<Module::CameraControl>();
  auto object = scene->create();
  object.add<Geometry>(app.assets().add(MeshData::cube()));
  object.add<Material>(Materials::Gold());
  auto lamp = scene->create();
  lamp.add<Transform>().position = {0, 3, 0};
  lamp.add<Geometry>(app.assets().add(MeshData::cube()));
  lamp.add<Material>(Materials::Emissive({4, 3, 2}));
  const auto original = scene->camera().get<Camera>();
  bool traced = false, restored = false;
  app.on<FrameUpdate>([&](const auto &frame) {
    if (frame.frame == 2)
      tap(GLFW_KEY_I);
    if (frame.frame == 8) {
      EXPECT_EQ(app.activeRenderer()->name(), "RayTracing");
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!app.renderProgress().samplesPerPixel && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      traced = app.renderProgress().samplesPerPixel == 1;
      tap(GLFW_KEY_F);
    }
    if (frame.frame == 12)
      tap(GLFW_KEY_I);
    if (frame.frame == 15) {
      EXPECT_TRUE(app.wireframeEnabled());
      EXPECT_EQ(app.activeRenderer()->name(), "Rasterization");
      EXPECT_EQ(app.renderProgress().completedFrames, 0u);
      tap(GLFW_KEY_F);
    }
    if (frame.frame == 20) {
      EXPECT_FALSE(app.wireframeEnabled());
      EXPECT_GT(app.renderProgress().completedFrames, 0u);
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
      EXPECT_NE(std::string_view(glfwGetWindowTitle(glfwGetCurrentContext())).find("Rasterization"),
                std::string_view::npos);
    }
    if (frame.frame > 30 && frame.frame % 30 == 1)
      tap(GLFW_KEY_I);
  });
  app.Run({240});
  EXPECT_TRUE(traced);
  EXPECT_TRUE(restored);
}
