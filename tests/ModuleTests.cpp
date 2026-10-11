#include <Jade/Renderer/RayTracing/RayTracing.hpp>
#include <Jade/Application.hpp>
#include <Jade/Core/Transform.hpp>
#include <Jade/Graphics/Motion.hpp>
#include <Jade/Module/FreeCameraControl.hpp>
#include <Jade/Module/FPSCameraControl.hpp>

#include <gtest/gtest.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

Jade::EngineConfig Hidden(bool debug = false)
{
  Jade::EngineConfig config;
  config.window.visible = false;
  config.window.width = 160;
  config.window.height = 120;
  config.frameLimits = 120;
  config.logPerformance = false;
  config.debug.axes = false;
  config.debug.shortcuts = debug;
  return config;
}

struct Input {
  GLFWwindow *window = glfwGetCurrentContext();
  GLFWkeyfun key;
  GLFWwindowfocusfun focus;
  GLFWcursorposfun cursor;
  GLFWmousebuttonfun button;
  GLFWscrollfun scroll;

  Input()
  {
    glfwPollEvents();
    key = glfwSetKeyCallback(window, nullptr);
    glfwSetKeyCallback(window, key);
    focus = glfwSetWindowFocusCallback(window, nullptr);
    glfwSetWindowFocusCallback(window, focus);
    cursor = glfwSetCursorPosCallback(window, nullptr);
    glfwSetCursorPosCallback(window, cursor);
    button = glfwSetMouseButtonCallback(window, nullptr);
    glfwSetMouseButtonCallback(window, button);
    scroll = glfwSetScrollCallback(window, nullptr);
    glfwSetScrollCallback(window, scroll);
    focus(window, GLFW_TRUE);
  }
  void Key(int code, int action, int mods = 0) { key(window, code, 0, action, mods); }
  void Tap(int code, int mods = 0)
  {
    Key(code, GLFW_PRESS, mods);
    Key(code, GLFW_REPEAT, mods);
    Key(code, GLFW_RELEASE, mods);
  }
  void Move(double x, double y) { cursor(window, x, y); }
  void Button(int action) { button(window, GLFW_MOUSE_BUTTON_LEFT, action, 0); }
};

Jade::Entity AddCamera(Jade::Scene &scene)
{
  const auto camera = scene.create();
  camera.add<Jade::Transform>().position = {0, 0, 5};
  camera.add<Jade::Camera>();
  return camera;
}

std::shared_ptr<Jade::Scene> SelectScene(Jade::Application &app)
{
  auto scene = std::make_shared<Jade::Scene>();
  scene->setCamera(AddCamera(*scene));
  app.runtime().setScene(scene);
  return scene;
}

void ExpectNear(Jade::Vec3 actual, Jade::Vec3 expected)
{
  EXPECT_NEAR(actual.x, expected.x, 1e-5f);
  EXPECT_NEAR(actual.y, expected.y, 1e-5f);
  EXPECT_NEAR(actual.z, expected.z, 1e-5f);
}

struct CounterModule {
  CounterModule() = delete; // Installation does not construct a Module instance.
  inline static int installs = 0;
  inline static int updates = 0;
  inline static Jade::EventConnection connection;
  static void attach(Jade::Scene &scene)
  {
    ++installs;
    connection = scene.on<Jade::FrameUpdate>([] { ++updates; });
  }
};

struct OuterModule {
  inline static bool fail = false;
  inline static Jade::EventConnection connection;
  static void attach(Jade::Scene &scene)
  {
    scene.use<CounterModule>();
    connection = scene.on<Jade::FrameUpdate>([] {});
    if (fail)
      throw std::runtime_error("module installation failed");
  }
};

struct RecursiveModule {
  inline static bool recurse = true;
  inline static Jade::EventConnection connection;
  static void attach(Jade::Scene &scene)
  {
    connection = scene.on<Jade::FrameUpdate>([] {});
    if (recurse)
      scene.use<RecursiveModule>();
  }
};

struct FailingFPSModule {
  static void attach(Jade::Scene &scene)
  {
    scene.use<Jade::Module::FPSCameraControl>();
    throw std::runtime_error("installation failed after FPS controls");
  }
};

} // namespace

TEST(SceneModules, InstallsOncePerSceneAndKeepsSubscriptionsAcrossActivation)
{
  CounterModule::installs = CounterModule::updates = 0;
  Jade::Application app(Hidden());
  auto a = std::make_shared<Jade::Scene>(), b = std::make_shared<Jade::Scene>();
  a->use<CounterModule>();
  a->use<CounterModule>();
  b->use<CounterModule>();
  EXPECT_EQ(CounterModule::installs, 2);
  app.runtime().setScene(a);
  app.Run({1});
  app.runtime().setScene(b);
  app.Run({1});
  app.runtime().setScene(a);
  a->use<CounterModule>();
  app.Run({1});
  EXPECT_EQ(CounterModule::installs, 2);
  EXPECT_EQ(CounterModule::updates, 3);
}

TEST(SceneModules, FailureCancelsNestedInstallationsAndPreservesExistingSubscribers)
{
  CounterModule::installs = CounterModule::updates = 0;
  OuterModule::fail = true;
  Jade::Application app(Hidden());
  auto scene = std::make_shared<Jade::Scene>();
  int existingUpdates = 0;
  const auto existing = scene->on<Jade::FrameUpdate>([&] { ++existingUpdates; });
  EXPECT_THROW(scene->use<OuterModule>(), std::runtime_error);
  EXPECT_FALSE(CounterModule::connection.connected());
  EXPECT_FALSE(OuterModule::connection.connected());
  EXPECT_TRUE(existing.connected());
  app.runtime().setScene(scene);
  app.Run({1});
  EXPECT_EQ(existingUpdates, 1);
  EXPECT_EQ(CounterModule::updates, 0);
  OuterModule::fail = false;
  scene->use<OuterModule>();
  scene->use<CounterModule>();
  app.Run({1});
  EXPECT_EQ(CounterModule::installs, 2);
  EXPECT_EQ(CounterModule::updates, 1);
  EXPECT_TRUE(OuterModule::connection.connected());
  const auto expired = CounterModule::connection;
  app.runtime().setScene(nullptr);
  scene.reset();
  EXPECT_FALSE(expired.connected());
}

TEST(SceneModules, FailureRetainsPreviouslyInstalledDependency)
{
  CounterModule::installs = CounterModule::updates = 0;
  Jade::Application app(Hidden());
  auto scene = std::make_shared<Jade::Scene>();
  scene->use<CounterModule>();
  const auto original = CounterModule::connection;
  OuterModule::fail = true;
  EXPECT_THROW(scene->use<OuterModule>(), std::runtime_error);
  EXPECT_TRUE(original.connected());
  EXPECT_FALSE(OuterModule::connection.connected());
  OuterModule::fail = false;
  scene->use<OuterModule>();
  app.runtime().setScene(scene);
  app.Run({1});
  EXPECT_EQ(CounterModule::installs, 1);
  EXPECT_EQ(CounterModule::updates, 1);
}

TEST(SceneModules, RejectsRecursiveInstallationAndAllowsRetry)
{
  Jade::Scene scene;
  RecursiveModule::recurse = true;
  EXPECT_THROW(scene.use<RecursiveModule>(), std::logic_error);
  EXPECT_FALSE(RecursiveModule::connection.connected());
  RecursiveModule::recurse = false;
  EXPECT_NO_THROW(scene.use<RecursiveModule>());
  EXPECT_TRUE(RecursiveModule::connection.connected());
}

TEST(ApplicationDebug, DebugShortcutsAreOptInAndWireframeAlwaysOperatesOncePerPress)
{
  EXPECT_FALSE(Jade::EngineConfig{}.debug.shortcuts);
  for (const bool debug : {false, true}) {
    Jade::Application app(Hidden(debug));
    const auto scene = SelectScene(app);
    auto &lens = scene->camera().get<Jade::Camera>();
    lens.fovDegrees = 47;
    lens.viewHeight = 9;
    Input input;
    input.Tap(GLFW_KEY_F);
    input.Tap(GLFW_KEY_F3);
    input.Tap(GLFW_KEY_F4);
    input.Tap(GLFW_KEY_O);
    app.Run({1});
    EXPECT_TRUE(app.runtime().renderer().wireframeEnabled());
    EXPECT_EQ(app.runtime().axesEnabled(), debug);
    EXPECT_EQ(app.runtime().aabbsEnabled(), debug);
    EXPECT_EQ(app.runtime().physicsDebugEnabled(), debug);
    EXPECT_EQ(lens.projection, debug ? Jade::Projection::Orthographic : Jade::Projection::Perspective);
    EXPECT_FLOAT_EQ(lens.fovDegrees, 47);
    EXPECT_FLOAT_EQ(lens.viewHeight, 9);
    input.Tap(GLFW_KEY_F);
    input.Tap(GLFW_KEY_F3);
    input.Tap(GLFW_KEY_F4);
    input.Tap(GLFW_KEY_O);
    app.Run({1});
    EXPECT_FALSE(app.runtime().renderer().wireframeEnabled());
    EXPECT_FALSE(app.runtime().axesEnabled());
    EXPECT_FALSE(app.runtime().aabbsEnabled());
    EXPECT_FALSE(app.runtime().physicsDebugEnabled());
    EXPECT_EQ(lens.projection, Jade::Projection::Perspective);
    app.runtime().renderer().setRender(false); app.runtime().renderer().setWireframe(true);
    EXPECT_TRUE(app.runtime().renderer().wireframeEnabled());
    input.Tap(GLFW_KEY_F4);
    app.Run({1});
    EXPECT_EQ(app.runtime().physicsDebugEnabled(), debug);
    EXPECT_TRUE(app.runtime().renderer().wireframeEnabled());
    EXPECT_FALSE(app.runtime().axesEnabled());
    EXPECT_FALSE(app.runtime().aabbsEnabled());
    if (debug) {
      input.Tap(GLFW_KEY_F);
      app.Run({1});
      EXPECT_FALSE(app.runtime().renderer().wireframeEnabled());
    }
  }
}

TEST(ApplicationDebug, ModifiersAndEventConsumptionPreventDefaults)
{
  Jade::Application app(Hidden(true));
  const auto scene = SelectScene(app);
  Input input;
  for (const int modifier : {GLFW_MOD_SHIFT, GLFW_MOD_CONTROL, GLFW_MOD_ALT, GLFW_MOD_SUPER}) {
    input.Tap(GLFW_KEY_F, modifier);
    input.Tap(GLFW_KEY_F3, modifier);
    input.Tap(GLFW_KEY_F4, modifier);
    input.Tap(GLFW_KEY_O, modifier);
  }
  app.Run({1});
  EXPECT_FALSE(app.runtime().renderer().wireframeEnabled());
  EXPECT_FALSE(app.runtime().axesEnabled());
  EXPECT_FALSE(app.runtime().physicsDebugEnabled());
  EXPECT_EQ(scene->camera().get<Jade::Camera>().projection, Jade::Projection::Perspective);
  int sceneF = 0;
  app.on<Jade::KeyPressed<"f">>([] { return Jade::EventFlow::Stop; });
  scene->on<Jade::KeyPressed<"f">>([&] { ++sceneF; });
  scene->on<Jade::KeyPressed<"F3">>([] { return Jade::EventFlow::Stop; });
  scene->on<Jade::KeyPressed<"F4">>([] { return Jade::EventFlow::Stop; });
  scene->on<Jade::KeyPressed<"o">>([] { return Jade::EventFlow::Stop; });
  input.Tap(GLFW_KEY_F);
  input.Tap(GLFW_KEY_F3);
  input.Tap(GLFW_KEY_F4);
  input.Tap(GLFW_KEY_O);
  app.Run({1});
  EXPECT_EQ(sceneF, 0);
  EXPECT_FALSE(app.runtime().renderer().wireframeEnabled());
  EXPECT_FALSE(app.runtime().axesEnabled());
  EXPECT_FALSE(app.runtime().physicsDebugEnabled());
  EXPECT_EQ(scene->camera().get<Jade::Camera>().projection, Jade::Projection::Perspective);
}

TEST(ApplicationDebug, CallbacksRunBeforeDefaultsAndStopCanPreventThem)
{
  Jade::Application app(Hidden(true));
  const auto scene = SelectScene(app);
  std::vector<int> order;
  app.on<Jade::KeyPressed<"f">>([&] {
    EXPECT_FALSE(app.runtime().renderer().wireframeEnabled());
    order.push_back(1);
  });
  scene->on<Jade::KeyPressed<"f">>([&] {
    EXPECT_FALSE(app.runtime().renderer().wireframeEnabled());
    order.push_back(2);
  });
  Input input;
  input.Tap(GLFW_KEY_F);
  app.Run({1});
  EXPECT_EQ(order, (std::vector<int>{1, 2}));
  EXPECT_TRUE(app.runtime().renderer().wireframeEnabled());
  app.on<Jade::KeyPressed<"F3">>([&] { app.stop(); });
  input.Tap(GLFW_KEY_F3);
  app.Run({1});
  EXPECT_FALSE(app.runtime().axesEnabled());
}

TEST(ApplicationDebug, ProjectionUsesRetainedSceneDuringDeferredSwitch)
{
  Jade::Application app(Hidden(true));
  auto a = SelectScene(app), b = std::make_shared<Jade::Scene>();
  b->setCamera(AddCamera(*b));
  app.on<Jade::KeyPressed<"2">>([&] { app.runtime().setScene(b); });
  Input input;
  input.Tap(GLFW_KEY_2);
  input.Tap(GLFW_KEY_O);
  app.Run({2});
  EXPECT_EQ(app.runtime().scene(), b);
  EXPECT_EQ(a->camera().get<Jade::Camera>().projection, Jade::Projection::Orthographic);
  EXPECT_EQ(b->camera().get<Jade::Camera>().projection, Jade::Projection::Perspective);
  input.Tap(GLFW_KEY_O);
  app.Run({1});
  EXPECT_EQ(b->camera().get<Jade::Camera>().projection, Jade::Projection::Orthographic);
}

TEST(ApplicationDebug, MissingCameraAndComponentsAreIgnored)
{
  Jade::Application app(Hidden(true));
  Input input;
  input.Tap(GLFW_KEY_O);
  EXPECT_NO_THROW(app.Run({1}));
  auto scene = std::make_shared<Jade::Scene>();
  app.runtime().setScene(scene);
  input.Tap(GLFW_KEY_O);
  EXPECT_NO_THROW(app.Run({1}));
  const auto camera = AddCamera(*scene);
  scene->setCamera(camera);
  camera.remove<Jade::Camera>();
  input.Tap(GLFW_KEY_O);
  EXPECT_NO_THROW(app.Run({1}));
  camera.add<Jade::Camera>();
  camera.remove<Jade::Transform>();
  input.Tap(GLFW_KEY_O);
  EXPECT_NO_THROW(app.Run({1}));
  EXPECT_EQ(camera.get<Jade::Camera>().projection, Jade::Projection::Perspective);
  scene->destroy(camera);
  input.Tap(GLFW_KEY_O);
  EXPECT_NO_THROW(app.Run({1}));
}

TEST(FreeCameraControl, MovementUsesDtAndExactBindingsWithoutDuplicateInstallation)
{
  Jade::Application app(Hidden());
  const auto scene = SelectScene(app);
  scene->use<Jade::Module::FreeCameraControl>();
  scene->use<Jade::Module::FreeCameraControl>();
  auto &pose = scene->camera().get<Jade::Transform>();
  float elapsed = 0;
  scene->on<Jade::FrameUpdate>([&](const auto &event) { elapsed += event.dt; });
  struct Movement {
    int key;
    int mods;
    Jade::Vec3 direction;
    float speed = 3;
  };
  const std::array movements{Movement{GLFW_KEY_W, 0, {0, 0, -1}},
                             Movement{GLFW_KEY_S, 0, {0, 0, 1}},
                             Movement{GLFW_KEY_A, 0, {-1, 0, 0}},
                             Movement{GLFW_KEY_D, 0, {1, 0, 0}},
                             Movement{GLFW_KEY_UP, 0, {0, 0, -1}},
                             Movement{GLFW_KEY_DOWN, 0, {0, 0, 1}},
                             Movement{GLFW_KEY_LEFT, 0, {-1, 0, 0}},
                             Movement{GLFW_KEY_RIGHT, 0, {1, 0, 0}},
                             Movement{GLFW_KEY_SPACE, 0, {0, 1, 0}},
                             Movement{GLFW_KEY_SPACE, GLFW_MOD_CONTROL, {0, -1, 0}},
                             Movement{GLFW_KEY_W, GLFW_MOD_SHIFT, {0, 0, -1}, 6},
                             Movement{GLFW_KEY_W, GLFW_MOD_CONTROL, {0, 0, -1}, 1}};
  Input input;
  for (const auto &movement : movements) {
    pose.position = {0, 0, 5};
    elapsed = 0;
    input.Key(movement.key, GLFW_PRESS, movement.mods);
    app.Run({2});
    EXPECT_GT(elapsed, 0);
    ExpectNear(pose.position, Jade::Vec3{0, 0, 5} + movement.direction * (elapsed * movement.speed));
    input.Key(movement.key, GLFW_RELEASE);
  }
}

TEST(FreeCameraControl, FollowsSelectedCameraForMovementRollAndZoom)
{
  Jade::Application app(Hidden());
  const auto scene = SelectScene(app);
  scene->use<Jade::Module::FreeCameraControl>();
  const auto first = scene->camera(), second = AddCamera(*scene);
  float elapsed = 0;
  scene->on<Jade::FrameUpdate>([&](const auto &event) {
    elapsed += event.dt;
    if (event.frame == 1)
      scene->setCamera(second);
  });
  Input input;
  input.Key(GLFW_KEY_W, GLFW_PRESS);
  app.Run({2});
  EXPECT_EQ(first.get<Jade::Transform>().position, (Jade::Vec3{0, 0, 5}));
  ExpectNear(second.get<Jade::Transform>().position, {0, 0, 5 - 3 * elapsed});
  input.Key(GLFW_KEY_W, GLFW_RELEASE);
  elapsed = 0;
  input.Key(GLFW_KEY_E, GLFW_PRESS);
  app.Run({2});
  Jade::Transform expected;
  Jade::Motion::roll(expected, 60.0 * elapsed);
  ExpectNear(Jade::Motion::getUp(second.get<Jade::Transform>()), Jade::Motion::getUp(expected));
  input.Key(GLFW_KEY_E, GLFW_RELEASE);
  elapsed = 0;
  input.Key(GLFW_KEY_Q, GLFW_PRESS);
  app.Run({2});
  Jade::Motion::roll(expected, -60.0 * elapsed);
  ExpectNear(Jade::Motion::getUp(second.get<Jade::Transform>()), Jade::Motion::getUp(expected));
  input.Key(GLFW_KEY_Q, GLFW_RELEASE);
  const float firstFov = first.get<Jade::Camera>().fovDegrees;
  auto zoomed = second.get<Jade::Camera>();
  Jade::Motion::zoom(zoomed, 2);
  input.scroll(input.window, 0, 2);
  app.Run({1});
  EXPECT_FLOAT_EQ(first.get<Jade::Camera>().fovDegrees, firstFov);
  EXPECT_FLOAT_EQ(second.get<Jade::Camera>().fovDegrees, zoomed.fovDegrees);
}

TEST(FreeCameraControl, DragUsesPixelDeltasAndResetsOnReleaseFocusAndCameraChange)
{
  Jade::Application app(Hidden());
  const auto scene = SelectScene(app);
  scene->use<Jade::Module::FreeCameraControl>();
  const auto first = scene->camera(), second = AddCamera(*scene);
  Input input;
  input.Move(0, 0);
  input.Button(GLFW_PRESS);
  input.Move(20, -10);
  input.Button(GLFW_RELEASE);
  input.Move(40, -20);
  app.Run({1});
  Jade::Transform expected;
  Jade::Motion::yaw(expected, 2);
  Jade::Motion::pitch(expected, 1);
  ExpectNear(Jade::Motion::getFront(first.get<Jade::Transform>()), Jade::Motion::getFront(expected));
  input.Button(GLFW_PRESS);
  app.Run({1});
  scene->setCamera(second);
  input.Move(60, -30);
  app.Run({1});
  ExpectNear(Jade::Motion::getFront(second.get<Jade::Transform>()), {0, 0, -1});
  input.Button(GLFW_RELEASE);
  input.Button(GLFW_PRESS);
  input.Move(70, -30);
  app.Run({1});
  Jade::Transform turned;
  Jade::Motion::yaw(turned, 1);
  ExpectNear(Jade::Motion::getFront(second.get<Jade::Transform>()), Jade::Motion::getFront(turned));
  input.focus(input.window, GLFW_FALSE);
  app.Run({1});
  input.focus(input.window, GLFW_TRUE);
  input.Move(100, 0);
  input.Move(120, 0);
  app.Run({1});
  ExpectNear(Jade::Motion::getFront(second.get<Jade::Transform>()), Jade::Motion::getFront(turned));
}

TEST(FreeCameraControl, SceneReactivationResetsDragAndDoesNotRetainOwner)
{
  Jade::Application app(Hidden());
  auto a = SelectScene(app), b = std::make_shared<Jade::Scene>();
  b->setCamera(AddCamera(*b));
  a->use<Jade::Module::FreeCameraControl>();
  b->use<Jade::Module::FreeCameraControl>();
  Input input;
  input.Move(0, 0);
  input.Button(GLFW_PRESS);
  app.Run({1});
  app.runtime().setScene(b);
  input.Move(20, 0);
  app.Run({1});
  ExpectNear(Jade::Motion::getFront(b->camera().get<Jade::Transform>()), {0, 0, -1});
  app.runtime().setScene(a);
  input.Move(40, 0);
  app.Run({1});
  ExpectNear(Jade::Motion::getFront(a->camera().get<Jade::Transform>()), {0, 0, -1});
  const auto connection = a->on<Jade::FrameUpdate>([] {});
  std::weak_ptr<Jade::Scene> weak = a;
  app.runtime().setScene(nullptr);
  a.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(connection.connected());
}

TEST(FreeCameraControl, InstallationNeedsNoCameraAndMissingComponentsAreSafe)
{
  Jade::Application app(Hidden());
  auto scene = std::make_shared<Jade::Scene>();
  scene->use<Jade::Module::FreeCameraControl>();
  app.runtime().setScene(scene);
  Input input;
  input.Key(GLFW_KEY_W, GLFW_PRESS);
  input.Move(0, 0);
  input.Button(GLFW_PRESS);
  input.Move(20, 0);
  input.scroll(input.window, 0, 1);
  EXPECT_NO_THROW(app.Run({2}));
  const auto camera = AddCamera(*scene);
  scene->setCamera(camera);
  input.Move(40, 0);
  app.Run({1});
  ExpectNear(Jade::Motion::getFront(camera.get<Jade::Transform>()), {0, 0, -1});
  camera.remove<Jade::Transform>();
  input.Move(60, 0);
  EXPECT_NO_THROW(app.Run({2}));
  camera.remove<Jade::Camera>();
  input.scroll(input.window, 0, 1);
  EXPECT_NO_THROW(app.Run({1}));
  scene->destroy(camera);
  EXPECT_NO_THROW(app.Run({1}));
}

TEST(FreeCameraControl, ScrollOutAndBackPastZoomLimitsRestoresProjectionAndPose)
{
  Jade::Application app(Hidden());
  auto scene = std::make_shared<Jade::Scene>();
  const auto camera = AddCamera(*scene);
  scene->setCamera(camera);
  scene->use<Jade::Module::FreeCameraControl>();
  app.runtime().setScene(scene);
  Input input;
  const auto pose = camera.get<Jade::Transform>();
  for (auto projection : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
    auto &lens = camera.get<Jade::Camera>();
    lens.projection = projection;
    lens.fovDegrees = 60;
    lens.viewHeight = 6;
    const auto original = lens;
    for (int i = 0; i < 4; ++i) {
      input.scroll(input.window, 0, -20);
      app.Run({1});
    }
    if (projection == Jade::Projection::Perspective)
      EXPECT_LE(lens.fovDegrees, 120);
    for (int i = 0; i < 4; ++i) {
      input.scroll(input.window, 0, 20);
      app.Run({1});
    }
    EXPECT_NEAR(lens.fovDegrees, original.fovDegrees, 1e-4);
    EXPECT_NEAR(lens.viewHeight, original.viewHeight, 1e-4);
    EXPECT_EQ(camera.get<Jade::Transform>().position, pose.position);
    EXPECT_EQ(camera.get<Jade::Transform>().rotation, pose.rotation);
  }
}

TEST(CameraControlModules, RejectsMixedControlsAndRollsBackFailedDependencies)
{
  Jade::Scene free, fps, retry;
  free.use<Jade::Module::FreeCameraControl>();
  EXPECT_THROW(free.use<Jade::Module::FPSCameraControl>(), std::logic_error);
  EXPECT_NO_THROW(free.use<Jade::Module::FreeCameraControl>());
  fps.use<Jade::Module::FPSCameraControl>();
  EXPECT_THROW(fps.use<Jade::Module::FreeCameraControl>(), std::logic_error);
  EXPECT_NO_THROW(fps.use<Jade::Module::FPSCameraControl>());
  EXPECT_THROW(retry.use<FailingFPSModule>(), std::runtime_error);
  EXPECT_NO_THROW(retry.use<Jade::Module::FreeCameraControl>());
}

TEST(FPSCameraControl, MovementIsHorizontalAndKeepsFullSpeedWhileLookingUp)
{
  Jade::Application app(Hidden());
  const auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  scene->use<Jade::Module::FPSCameraControl>();
  auto &pose = scene->camera().get<Jade::Transform>();
  pose.rotation = Jade::Quaternion::angleAxis({0, 1, 0}, -45) *
                  Jade::Quaternion::angleAxis({1, 0, 0}, 60);
  Jade::Motion::roll(pose, 30);
  float elapsed = 0;
  scene->on<Jade::FrameUpdate>([&](const auto &event) { elapsed += event.dt; });
  const float diagonal = std::sqrt(0.5f);
  const Jade::Vec3 front{diagonal, 0, -diagonal}, right{diagonal, 0, diagonal};
  struct Movement {
    int key;
    int mods;
    Jade::Vec3 direction;
    float speed = 3;
  };
  const std::array movements{Movement{GLFW_KEY_W, 0, front},
                             Movement{GLFW_KEY_S, 0, -front},
                             Movement{GLFW_KEY_A, 0, -right},
                             Movement{GLFW_KEY_D, 0, right},
                             Movement{GLFW_KEY_UP, 0, front},
                             Movement{GLFW_KEY_DOWN, 0, -front},
                             Movement{GLFW_KEY_LEFT, 0, -right},
                             Movement{GLFW_KEY_RIGHT, 0, right},
                             Movement{GLFW_KEY_W, GLFW_MOD_SHIFT, front, 6},
                             Movement{GLFW_KEY_W, GLFW_MOD_CONTROL, front, 1}};
  Input input;
  for (const auto &movement : movements) {
    pose.position = {0, 7, 5};
    elapsed = 0;
    input.Key(movement.key, GLFW_PRESS, movement.mods);
    app.Run({2});
    EXPECT_GT(elapsed, 0);
    ExpectNear(pose.position, Jade::Vec3{0, 7, 5} + movement.direction * (elapsed * movement.speed));
    EXPECT_FLOAT_EQ(pose.position.y, 7);
    ExpectNear(Jade::Motion::getRight(pose), right);
    input.Key(movement.key, GLFW_RELEASE);
  }
}

TEST(FPSCameraControl, MouseLookNeedsNoButtonClampsPitchAndYawsAroundParentUp)
{
  Jade::Application app(Hidden());
  const auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  // Install on an already active Scene, then synchronize capture before input.
  app.Run({1});
  Input input;
  EXPECT_EQ(glfwGetInputMode(input.window, GLFW_CURSOR), GLFW_CURSOR_DISABLED);
  auto &pose = scene->camera().get<Jade::Transform>();
  input.Move(0, 0);
  input.Move(20, -10);
  app.Run({1});
  Jade::Transform expected;
  Jade::Motion::yaw(expected, 2);
  Jade::Motion::pitch(expected, 1);
  ExpectNear(Jade::Motion::getFront(pose), Jade::Motion::getFront(expected));
  input.Move(20, -10000);
  app.Run({1});
  expected.rotation = Jade::Quaternion::angleAxis({0, 1, 0}, -2) *
                      Jade::Quaternion::angleAxis({1, 0, 0}, 89);
  ExpectNear(Jade::Motion::getFront(pose), Jade::Motion::getFront(expected));
  input.Move(320, -11000);
  app.Run({1});
  expected.rotation = Jade::Quaternion::angleAxis({0, 1, 0}, -32) *
                      Jade::Quaternion::angleAxis({1, 0, 0}, 89);
  ExpectNear(Jade::Motion::getFront(pose), Jade::Motion::getFront(expected));
  EXPECT_NEAR(Jade::Motion::getRight(pose).y, 0, 1e-6);
  input.Move(320, 11000);
  app.Run({1});
  expected.rotation = Jade::Quaternion::angleAxis({0, 1, 0}, -32) *
                      Jade::Quaternion::angleAxis({1, 0, 0}, -89);
  ExpectNear(Jade::Motion::getFront(pose), Jade::Motion::getFront(expected));
  const auto stationary = pose.rotation;
  input.Move(320, 11000); // Zero mouse delta must not rewrite orientation.
  app.Run({3});
  EXPECT_EQ(pose.rotation, stationary);
}

TEST(FPSCameraControl, RollAndFlightKeysDoNotChangePoseOrHandleJump)
{
  Jade::Application app(Hidden());
  const auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  app.Run({1});
  Input input;
  const auto initial = scene->camera().get<Jade::Transform>();
  for (const int key : {GLFW_KEY_Q, GLFW_KEY_E, GLFW_KEY_SPACE}) {
    input.Key(key, GLFW_PRESS);
    app.Run({2});
    input.Key(key, GLFW_RELEASE);
  }
  input.Key(GLFW_KEY_SPACE, GLFW_PRESS, GLFW_MOD_CONTROL);
  app.Run({2});
  input.Key(GLFW_KEY_SPACE, GLFW_RELEASE);
  const auto &pose = scene->camera().get<Jade::Transform>();
  ExpectNear(pose.position, initial.position);
  ExpectNear(Jade::Motion::getUp(pose), Jade::Motion::getUp(initial));
  // Space remains available to a future character controller.
  int jumps = 0;
  scene->on<Jade::KeyPressed<"Space">>([&] { ++jumps; });
  input.Tap(GLFW_KEY_SPACE);
  app.Run({1});
  EXPECT_EQ(jumps, 1);
}

TEST(FPSCameraControl, FollowsCameraChangesAndExternalPoseEditsWithoutRetainingComponents)
{
  Jade::Application app(Hidden());
  const auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  app.Run({1});
  const auto first = scene->camera(), second = AddCamera(*scene);
  Input input;
  input.Move(0, 0);
  input.Move(20, 0);
  app.Run({1});
  scene->setCamera(second);
  input.Move(40, 0);
  app.Run({1});
  Jade::Transform expected;
  Jade::Motion::yaw(expected, 2);
  ExpectNear(Jade::Motion::getFront(first.get<Jade::Transform>()), Jade::Motion::getFront(expected));
  ExpectNear(Jade::Motion::getFront(second.get<Jade::Transform>()), Jade::Motion::getFront(expected));
  auto &pose = second.get<Jade::Transform>();
  pose.rotation = Jade::Quaternion::angleAxis({1, 0, 0}, 90);
  Jade::Motion::roll(pose, 45);
  app.Run({1});
  EXPECT_NEAR(Jade::Motion::getRight(pose).y, 0, 1e-6);
  EXPECT_LT(Jade::Motion::getFront(pose).y, 1);
  input.Key(GLFW_KEY_W, GLFW_PRESS);
  second.remove<Jade::Transform>();
  input.Move(60, 0);
  EXPECT_NO_THROW(app.Run({2}));
  scene->destroy(second);
  input.Move(80, 0);
  EXPECT_NO_THROW(app.Run({2}));
}

TEST(FPSCameraControl, CaptureAndMouseBaselineFollowSceneAndFocusLifecycle)
{
  Jade::Application app(Hidden());
  auto fps = std::make_shared<Jade::Scene>(), free = std::make_shared<Jade::Scene>();
  fps->use<Jade::Module::FPSCameraControl>();
  free->use<Jade::Module::FreeCameraControl>();
  app.runtime().setScene(fps);
  Input input;
  EXPECT_EQ(glfwGetInputMode(input.window, GLFW_CURSOR), GLFW_CURSOR_DISABLED);
  input.Key(GLFW_KEY_W, GLFW_PRESS);
  input.Move(0, 0);
  input.Move(20, -10);
  EXPECT_NO_THROW(app.Run({2})); // Empty scenes can install and receive input.
  const auto camera = AddCamera(*fps);
  fps->setCamera(camera);
  input.Key(GLFW_KEY_W, GLFW_RELEASE);
  input.focus(input.window, GLFW_FALSE);
  input.Move(500, 500);
  app.Run({1});
  input.focus(input.window, GLFW_TRUE);
  input.Move(1000, 1000);
  input.Move(1020, 1000);
  app.Run({1});
  Jade::Transform expected;
  Jade::Motion::yaw(expected, 2);
  ExpectNear(Jade::Motion::getFront(camera.get<Jade::Transform>()), Jade::Motion::getFront(expected));
  app.runtime().setScene(free);
  EXPECT_EQ(glfwGetInputMode(input.window, GLFW_CURSOR), GLFW_CURSOR_NORMAL);
  app.runtime().setScene(fps);
  EXPECT_EQ(glfwGetInputMode(input.window, GLFW_CURSOR), GLFW_CURSOR_DISABLED);
  input.Move(5000, 5000);
  app.Run({1});
  ExpectNear(Jade::Motion::getFront(camera.get<Jade::Transform>()), Jade::Motion::getFront(expected));
  std::weak_ptr<Jade::Scene> weak = fps;
  const auto connection = fps->on<Jade::FrameUpdate>([] {});
  app.runtime().setScene(nullptr);
  EXPECT_EQ(glfwGetInputMode(input.window, GLFW_CURSOR), GLFW_CURSOR_NORMAL);
  fps.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(connection.connected());
}
