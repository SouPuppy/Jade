#include <Jade/Renderer/RayTracing/RayTracing.hpp>
#include <Jade/Application.hpp>
#include <Jade/Core/Transform.hpp>
#include <Jade/Graphics/Motion.hpp>
#include <Jade/Graphics/CameraControl.hpp>
#include <Jade/Graphics/CameraMotion.hpp>
#include <Jade/Module/FPSCameraControl.hpp>

#include <gtest/gtest.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cmath>
#include <limits>
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
  void Button(int action) { button(window, GLFW_MOUSE_BUTTON_RIGHT, action, 0); }
};

Jade::Entity AddCamera(Jade::Scene &scene)
{
  const auto camera = scene.world().create();
  camera.add<Jade::Transform>().position = {0, 0, 5};
  camera.add<Jade::Camera>();
  return camera;
}

std::shared_ptr<Jade::Scene> SelectScene(Jade::Application &app)
{
  auto scene = std::make_shared<Jade::Scene>(std::make_shared<Jade::World>());
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
  auto a = std::make_shared<Jade::Scene>(std::make_shared<Jade::World>()), b = std::make_shared<Jade::Scene>(std::make_shared<Jade::World>());
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
  auto scene = std::make_shared<Jade::Scene>(std::make_shared<Jade::World>());
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
  auto scene = std::make_shared<Jade::Scene>(std::make_shared<Jade::World>());
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
  Jade::Scene scene{std::make_shared<Jade::World>()};
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
    input.Tap(GLFW_KEY_F1);
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
    input.Tap(GLFW_KEY_F1);
    input.Tap(GLFW_KEY_F3);
    input.Tap(GLFW_KEY_F4);
    input.Tap(GLFW_KEY_O);
    app.Run({1});
    EXPECT_FALSE(app.runtime().renderer().wireframeEnabled());
    EXPECT_FALSE(app.runtime().axesEnabled());
    EXPECT_FALSE(app.runtime().aabbsEnabled());
    EXPECT_FALSE(app.runtime().physicsDebugEnabled());
    EXPECT_EQ(lens.projection, Jade::Projection::Perspective);
    app.runtime().renderer().setRayTracingEnabled(false); app.runtime().renderer().setWireframe(true);
    EXPECT_TRUE(app.runtime().renderer().wireframeEnabled());
    input.Tap(GLFW_KEY_F4);
    app.Run({1});
    EXPECT_EQ(app.runtime().physicsDebugEnabled(), debug);
    EXPECT_TRUE(app.runtime().renderer().wireframeEnabled());
    EXPECT_FALSE(app.runtime().axesEnabled());
    EXPECT_FALSE(app.runtime().aabbsEnabled());
    if (debug) {
      input.Tap(GLFW_KEY_F1);
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
    input.Tap(GLFW_KEY_F1, modifier);
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
  app.on<Jade::KeyPressed<"F1">>([] { return Jade::EventFlow::Stop; });
  scene->on<Jade::KeyPressed<"F1">>([&] { ++sceneF; });
  scene->on<Jade::KeyPressed<"F3">>([] { return Jade::EventFlow::Stop; });
  scene->on<Jade::KeyPressed<"F4">>([] { return Jade::EventFlow::Stop; });
  scene->on<Jade::KeyPressed<"o">>([] { return Jade::EventFlow::Stop; });
  input.Tap(GLFW_KEY_F1);
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
  app.on<Jade::KeyPressed<"F1">>([&] {
    EXPECT_FALSE(app.runtime().renderer().wireframeEnabled());
    order.push_back(1);
  });
  scene->on<Jade::KeyPressed<"F1">>([&] {
    EXPECT_FALSE(app.runtime().renderer().wireframeEnabled());
    order.push_back(2);
  });
  Input input;
  input.Tap(GLFW_KEY_F1);
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
  auto a = SelectScene(app), b = std::make_shared<Jade::Scene>(std::make_shared<Jade::World>());
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
  auto scene = std::make_shared<Jade::Scene>(std::make_shared<Jade::World>());
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
  scene->world().destroy(camera);
  input.Tap(GLFW_KEY_O);
  EXPECT_NO_THROW(app.Run({1}));
}

TEST(FPSCameraControl, MovementUsesDtAndModifiersDoNotDisableBindings)
{
  Jade::Application app(Hidden());
  auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  scene->use<Jade::Module::FPSCameraControl>();
  auto& pose = scene->camera().get<Jade::Transform>();
  float elapsed = 0;
  scene->on<Jade::FrameUpdate>([&](const auto& frame) { elapsed += frame.dt; });
  struct Movement { int key, mods; Jade::Vec3 direction; float speed = 3; };
  const std::array movements{
    Movement{GLFW_KEY_W, 0, {0,0,-1}}, Movement{GLFW_KEY_S, 0, {0,0,1}},
    Movement{GLFW_KEY_A, 0, {-1,0,0}}, Movement{GLFW_KEY_D, 0, {1,0,0}},
    Movement{GLFW_KEY_UP, 0, {0,0,-1}}, Movement{GLFW_KEY_DOWN, 0, {0,0,1}},
    Movement{GLFW_KEY_LEFT, 0, {-1,0,0}}, Movement{GLFW_KEY_RIGHT, 0, {1,0,0}},
    Movement{GLFW_KEY_SPACE, 0, {0,1,0}}, Movement{GLFW_KEY_LEFT_SHIFT, GLFW_MOD_SHIFT, {0,-1,0}},
    Movement{GLFW_KEY_RIGHT_SHIFT, GLFW_MOD_SHIFT, {0,-1,0}},
    Movement{GLFW_KEY_W, GLFW_MOD_SHIFT, {0,0,-1}},
    Movement{GLFW_KEY_D, GLFW_MOD_CONTROL, {1,0,0}, 1}};
  Input input;
  for (const auto& movement : movements) {
    pose.position = {0,0,5}; elapsed = 0;
    input.Key(movement.key, GLFW_PRESS, movement.mods);
    app.Run({2});
    EXPECT_GT(elapsed, 0);
    ExpectNear(pose.position, Jade::Vec3{0,0,5} + movement.direction * (elapsed * movement.speed));
    input.Key(movement.key, GLFW_RELEASE);
  }
}

TEST(FPSCameraControl, CombinesDirectionsNormalizesDiagonalsAndDeduplicatesAliases)
{
  Jade::Application app(Hidden()); auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  auto& pose = scene->camera().get<Jade::Transform>();
  float elapsed = 0;
  scene->on<Jade::FrameUpdate>([&](const auto& frame) { elapsed += frame.dt; });
  Input input;
  input.Key(GLFW_KEY_W, GLFW_PRESS); input.Key(GLFW_KEY_UP, GLFW_PRESS);
  input.Key(GLFW_KEY_D, GLFW_PRESS); input.Key(GLFW_KEY_LEFT_SHIFT, GLFW_PRESS, GLFW_MOD_SHIFT);
  app.Run({3});
  const float distance = 3 * elapsed / std::sqrt(3.0f);
  ExpectNear(pose.position, {distance, -distance, 5-distance});
  input.Key(GLFW_KEY_W, GLFW_RELEASE); input.Key(GLFW_KEY_UP, GLFW_RELEASE);
  input.Key(GLFW_KEY_D, GLFW_RELEASE); input.Key(GLFW_KEY_LEFT_SHIFT, GLFW_RELEASE);
  const auto start = pose.position;
  input.Key(GLFW_KEY_W, GLFW_PRESS); input.Key(GLFW_KEY_S, GLFW_PRESS);
  input.Key(GLFW_KEY_SPACE, GLFW_PRESS); input.Key(GLFW_KEY_LEFT_SHIFT, GLFW_PRESS, GLFW_MOD_SHIFT);
  app.Run({3}); ExpectNear(pose.position, start);
}

TEST(FPSCameraControl, EditorLookClampsPitchLocksUpAndDoesNotFlip)
{
  Jade::Application app(Hidden()); auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  auto& pose = scene->camera().get<Jade::Transform>();
  Input input;
  EXPECT_EQ(glfwGetInputMode(input.window, GLFW_CURSOR), GLFW_CURSOR_NORMAL);
  input.Move(0,0); input.Move(20,-10); app.Run({1});
  ExpectNear(Jade::Motion::getFront(pose), {0,0,-1});
  input.Button(GLFW_PRESS); input.Move(40,-20); app.Run({1});
  Jade::Transform expected; Jade::CameraMotion::look(expected,2,1);
  ExpectNear(Jade::Motion::getFront(pose), Jade::Motion::getFront(expected));
  input.Move(40,-10000); app.Run({1});
  EXPECT_NEAR(Jade::Motion::getFront(pose).y, std::sin(89.0 * std::acos(-1.0) / 180), 1e-6);
  input.Move(340,-11000); app.Run({1});
  EXPECT_NEAR(Jade::Motion::getRight(pose).y, 0, 1e-6);
  EXPECT_GT(Jade::Motion::getUp(pose).y, 0);
  input.Move(340,11000); app.Run({1});
  EXPECT_LT(Jade::Motion::getFront(pose).y, -0.99f);
  EXPECT_GT(Jade::Motion::getUp(pose).y, 0);
  input.Button(GLFW_RELEASE); input.Move(400,12000); app.Run({1});
  const auto stationary = pose.rotation; app.Run({3}); EXPECT_EQ(pose.rotation, stationary);
}

TEST(FPSCameraControl, MovementStaysHorizontalWhenLookingUpAndRemovesExternalRoll)
{
  Jade::Application app(Hidden()); auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  auto& pose = scene->camera().get<Jade::Transform>();
  Jade::CameraMotion::look(pose,45,89); Jade::Motion::roll(pose,70);
  float elapsed = 0;
  scene->on<Jade::FrameUpdate>([&](const auto& frame) { elapsed += frame.dt; });
  Input input; input.Key(GLFW_KEY_W, GLFW_PRESS); app.Run({3});
  const float distance = 3 * elapsed / std::sqrt(2.0f);
  ExpectNear(pose.position, {distance,0,5-distance});
  EXPECT_NEAR(Jade::Motion::getRight(pose).y, 0, 1e-6);
}

TEST(FPSCameraControl, FirstPersonIsAnInteractionModeAndCaptureFollowsCameraAndScene)
{
  Jade::Application app(Hidden()); auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  const auto first = scene->camera(), second = AddCamera(*scene);
  first.add<Jade::CameraControl>().viewMode = Jade::ViewMode::FirstPerson;
  app.Run({1}); Input input;
  EXPECT_EQ(glfwGetInputMode(input.window, GLFW_CURSOR), GLFW_CURSOR_DISABLED);
  input.Move(0,0); input.Move(20,-10); app.Run({1});
  Jade::Transform expected; Jade::CameraMotion::look(expected,2,1);
  ExpectNear(Jade::Motion::getFront(first.get<Jade::Transform>()), Jade::Motion::getFront(expected));
  scene->setCamera(second); app.Run({1});
  EXPECT_EQ(glfwGetInputMode(input.window, GLFW_CURSOR), GLFW_CURSOR_NORMAL);
  scene->setCamera(first); app.Run({1});
  EXPECT_EQ(glfwGetInputMode(input.window, GLFW_CURSOR), GLFW_CURSOR_DISABLED);
  input.focus(input.window, GLFW_FALSE); app.Run({1}); input.focus(input.window, GLFW_TRUE);
  input.Move(1000,1000); app.Run({1});
  ExpectNear(Jade::Motion::getFront(first.get<Jade::Transform>()), Jade::Motion::getFront(expected));
  app.runtime().setScene(nullptr);
  EXPECT_EQ(glfwGetInputMode(input.window, GLFW_CURSOR), GLFW_CURSOR_NORMAL);
}

TEST(FPSCameraControl, BindingsBelongToCameraAndCanChangeWithoutReinstallation)
{
  Jade::Application app(Hidden()); auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  auto& settings = scene->camera().add<Jade::CameraControl>();
  settings.bindings[Jade::CameraAction::Forward] = {Jade::Key::I, Jade::Key::Unknown};
  settings.bindings.look = Jade::MouseButton::Left; settings.sensitivity = 0.5;
  settings.bindings.wheelZoom = false;
  Input input; input.Move(0,0); input.Button(GLFW_PRESS); input.Move(20,0); app.Run({1});
  ExpectNear(Jade::Motion::getFront(scene->camera().get<Jade::Transform>()), {0,0,-1});
  input.button(input.window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0); input.Move(40,0); app.Run({1});
  Jade::Transform expected; Jade::CameraMotion::look(expected,10,0);
  ExpectNear(Jade::Motion::getFront(scene->camera().get<Jade::Transform>()), Jade::Motion::getFront(expected));
  const auto start = scene->camera().get<Jade::Transform>().position;
  input.Key(GLFW_KEY_W, GLFW_PRESS); app.Run({2}); input.Key(GLFW_KEY_W, GLFW_RELEASE);
  ExpectNear(scene->camera().get<Jade::Transform>().position, start);
  const auto fov = scene->camera().get<Jade::Camera>().fovDegrees;
  input.scroll(input.window,0,2); app.Run({1}); EXPECT_FLOAT_EQ(scene->camera().get<Jade::Camera>().fovDegrees,fov);
  float elapsed = 0; scene->on<Jade::FrameUpdate>([&](const auto& frame) { elapsed += frame.dt; });
  input.Key(GLFW_KEY_I, GLFW_PRESS); app.Run({2});
  ExpectNear(scene->camera().get<Jade::Transform>().position, start + Jade::Motion::getFront(expected) * (elapsed*3));
}

TEST(FPSCameraControl, GlobalConsumptionSuppressesHeldMovement)
{
  Jade::Application app(Hidden()); auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  app.on<Jade::KeyHeld<"w">>([] { return Jade::EventFlow::Stop; });
  Input input; input.Key(GLFW_KEY_W, GLFW_PRESS); app.Run({3});
  ExpectNear(scene->camera().get<Jade::Transform>().position, {0,0,5});
}

TEST(FPSCameraControl, ConsumedFrameDoesNotCarryMovementIntoTheNextFrame)
{
  Jade::Application app(Hidden()); auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  Input input;
  app.on<Jade::FrameUpdate>([&](const auto& frame) {
    if (frame.frame == 2) {
      input.Key(GLFW_KEY_W, GLFW_RELEASE);
      return Jade::EventFlow::Stop;
    }
    return Jade::EventFlow::Continue;
  });
  input.Key(GLFW_KEY_W, GLFW_PRESS); app.Run({3});
  ExpectNear(scene->camera().get<Jade::Transform>().position, {0,0,5});
}

TEST(FPSCameraControl, DragResetsOnFocusCameraSceneAndSettingsChanges)
{
  Jade::Application app(Hidden()); auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  const auto second = AddCamera(*scene);
  Input input; input.Move(0,0); input.Button(GLFW_PRESS); app.Run({1});
  scene->setCamera(second); input.Move(20,0); app.Run({1});
  ExpectNear(Jade::Motion::getFront(second.get<Jade::Transform>()), {0,0,-1});
  input.Button(GLFW_RELEASE); input.Button(GLFW_PRESS); app.Run({1});
  input.focus(input.window,GLFW_FALSE); app.Run({1}); input.focus(input.window,GLFW_TRUE);
  input.Move(100,0); input.Move(120,0); app.Run({1});
  ExpectNear(Jade::Motion::getFront(second.get<Jade::Transform>()), {0,0,-1});
  input.Button(GLFW_PRESS); app.Run({1});
  app.runtime().setScene(nullptr); app.runtime().setScene(scene); input.Move(140,0); app.Run({1});
  ExpectNear(Jade::Motion::getFront(second.get<Jade::Transform>()), {0,0,-1});
  input.Button(GLFW_RELEASE); input.Button(GLFW_PRESS); app.Run({1});
  second.add<Jade::CameraControl>().sensitivity=0.2; input.Move(160,0); app.Run({1});
  ExpectNear(Jade::Motion::getFront(second.get<Jade::Transform>()), {0,0,-1});
}

TEST(FPSCameraControl, MiddlePanTracksOrthographicPixelsAndPerspectiveDepth)
{
  Jade::Application app(Hidden()); auto scene = SelectScene(app);
  scene->use<Jade::Module::FPSCameraControl>();
  auto& pose = scene->camera().get<Jade::Transform>(); auto& lens = scene->camera().get<Jade::Camera>();
  Jade::CameraMotion::look(pose,35,20); app.Run({1});
  Input input; int width=0,height=0; glfwGetWindowSize(input.window,&width,&height);
  for (const auto projection : {Jade::Projection::Orthographic,Jade::Projection::Perspective}) {
    lens.projection=projection; app.Run({1});
    const auto orientation=pose.rotation;
    const auto start=pose.position;
    const auto right=Jade::Motion::getRight(pose), up=Jade::Motion::getUp(pose);
    const float span=projection==Jade::Projection::Orthographic?6.0f:20*std::tan(std::acos(-1.0f)/6);
    input.Move(0,0); input.button(input.window,GLFW_MOUSE_BUTTON_MIDDLE,GLFW_PRESS,0);
    input.Move(width/4.0,height/4.0); input.button(input.window,GLFW_MOUSE_BUTTON_MIDDLE,GLFW_RELEASE,0);
    input.Move(width/2.0,height/2.0); app.Run({1});
    ExpectNear(pose.position,start-right*(span*width/height/4)+up*(span/4));
    EXPECT_EQ(pose.rotation,orientation);
  }
  lens.projection=Jade::Projection::Orthographic; lens.viewHeight=3; app.Run({1});
  glfwSetWindowSize(input.window,240,180); glfwPollEvents(); glfwGetWindowSize(input.window,&width,&height);
  const auto start=pose.position;
  input.Move(0,0); input.button(input.window,GLFW_MOUSE_BUTTON_MIDDLE,GLFW_PRESS,0);
  input.Move(0,height/3.0); input.button(input.window,GLFW_MOUSE_BUTTON_MIDDLE,GLFW_RELEASE,0); app.Run({1});
  ExpectNear(pose.position,start+Jade::Motion::getUp(pose));
}

TEST(FPSCameraControl, ProjectionChangeCancelsPanUntilANewPress)
{
  Jade::Application app(Hidden()); auto scene=SelectScene(app); scene->use<Jade::Module::FPSCameraControl>();
  auto& lens=scene->camera().get<Jade::Camera>(); Input input;
  input.Move(0,0); input.button(input.window,GLFW_MOUSE_BUTTON_MIDDLE,GLFW_PRESS,0); app.Run({1});
  lens.projection=Jade::Projection::Orthographic; input.Move(20,10); app.Run({1});
  ExpectNear(scene->camera().get<Jade::Transform>().position,{0,0,5});
  input.Move(40,20); app.Run({1}); ExpectNear(scene->camera().get<Jade::Transform>().position,{0,0,5});
}

TEST(FPSCameraControl, ScrollReversalRestoresBothProjectionsEvenPastLimits)
{
  Jade::Application app(Hidden()); auto scene=SelectScene(app); scene->use<Jade::Module::FPSCameraControl>();
  Input input; app.Run({1}); const auto pose=scene->camera().get<Jade::Transform>();
  for (auto projection : {Jade::Projection::Perspective,Jade::Projection::Orthographic}) {
    auto& lens=scene->camera().get<Jade::Camera>(); lens.projection=projection;
    lens.fovDegrees=60; lens.viewHeight=6; const auto original=lens;
    for(int i=0;i<4;++i) {input.scroll(input.window,0,-20);app.Run({1});}
    for(int i=0;i<4;++i) {input.scroll(input.window,0,20);app.Run({1});}
    EXPECT_NEAR(lens.fovDegrees,original.fovDegrees,1e-4); EXPECT_NEAR(lens.viewHeight,original.viewHeight,1e-4);
    EXPECT_EQ(scene->camera().get<Jade::Transform>().rotation,pose.rotation);
    EXPECT_EQ(scene->camera().get<Jade::Transform>().position,pose.position);
  }
}

TEST(FPSCameraControl, MissingComponentsAndSceneOwnershipAreSafe)
{
  Jade::Application app(Hidden()); auto scene=std::make_shared<Jade::Scene>(std::make_shared<Jade::World>());
  scene->use<Jade::Module::FPSCameraControl>(); app.runtime().setScene(scene);
  Input input; input.Key(GLFW_KEY_W,GLFW_PRESS); input.Button(GLFW_PRESS);
  input.Move(0,0);input.Move(20,0); input.scroll(input.window,0,1); EXPECT_NO_THROW(app.Run({2}));
  const auto camera=AddCamera(*scene);scene->setCamera(camera);input.Move(40,0);app.Run({1});
  ExpectNear(Jade::Motion::getFront(camera.get<Jade::Transform>()),{0,0,-1});
  camera.remove<Jade::Transform>();input.Move(60,0);EXPECT_NO_THROW(app.Run({2}));
  camera.remove<Jade::Camera>();input.scroll(input.window,0,1);EXPECT_NO_THROW(app.Run({1}));
  scene->world().destroy(camera);EXPECT_NO_THROW(app.Run({1}));
  const auto connection=scene->on<Jade::FrameUpdate>([]{});std::weak_ptr<Jade::Scene> weak=scene;
  app.runtime().setScene(nullptr);scene.reset();EXPECT_TRUE(weak.expired());EXPECT_FALSE(connection.connected());
}

TEST(FPSCameraControl, FailedDependencyRollsBackControlsAndAllowsRetry)
{
  Jade::Application app(Hidden());auto scene=SelectScene(app);
  scene->camera().add<Jade::CameraControl>().viewMode=Jade::ViewMode::FirstPerson;
  EXPECT_THROW(scene->use<FailingFPSModule>(),std::runtime_error);
  app.Run({1});Input input;EXPECT_EQ(glfwGetInputMode(input.window,GLFW_CURSOR),GLFW_CURSOR_NORMAL);
  scene->use<Jade::Module::FPSCameraControl>();app.Run({1});
  EXPECT_EQ(glfwGetInputMode(input.window,GLFW_CURSOR),GLFW_CURSOR_DISABLED);
}

TEST(CameraObserver, SharesControlsAndIsolatesGamePoseZoomAndDrag)
{
  Jade::Application app(Hidden(true));auto scene=SelectScene(app);scene->use<Jade::Module::FPSCameraControl>();
  Input input; input.Move(0,0); input.Button(GLFW_PRESS);app.Run({1});
  const auto gamePose=scene->camera().get<Jade::Transform>(); const auto gameLens=scene->camera().get<Jade::Camera>();
  app.runtime().setSoulMode(true); app.Run({1});
  input.Move(0,0); input.Button(GLFW_RELEASE);input.Button(GLFW_PRESS); input.Move(20,-10000);
  input.scroll(input.window,0,2);app.Run({1});
  ASSERT_NE(app.runtime().soulCamera(),nullptr);
  EXPECT_GT(app.runtime().soulCamera()->up.y,0); EXPECT_NEAR(app.runtime().soulCamera()->right.y,0,1e-6);
  EXPECT_LT(app.runtime().soulCamera()->lens.fovDegrees,gameLens.fovDegrees);
  const auto start=app.runtime().soulCamera()->position;
  float elapsed=0;scene->on<Jade::FrameUpdate>([&](const auto& f){elapsed+=f.dt;});
  input.Key(GLFW_KEY_SPACE,GLFW_PRESS);app.Run({2});input.Key(GLFW_KEY_SPACE,GLFW_RELEASE);
  ExpectNear(app.runtime().soulCamera()->position,start+Jade::Vec3{0,3*elapsed,0});
  const auto afterUp=app.runtime().soulCamera()->position;elapsed=0;
  input.Key(GLFW_KEY_LEFT_SHIFT,GLFW_PRESS,GLFW_MOD_SHIFT);app.Run({2});input.Key(GLFW_KEY_LEFT_SHIFT,GLFW_RELEASE);
  ExpectNear(app.runtime().soulCamera()->position,afterUp-Jade::Vec3{0,3*elapsed,0});
  EXPECT_EQ(scene->camera().get<Jade::Transform>().position,gamePose.position);
  EXPECT_EQ(scene->camera().get<Jade::Transform>().rotation,gamePose.rotation);
  EXPECT_EQ(scene->camera().get<Jade::Camera>(),gameLens);
  input.focus(input.window,GLFW_FALSE);app.Run({1});input.focus(input.window,GLFW_TRUE);
  const auto front=app.runtime().soulCamera()->forward;
  input.Move(1000,1000);input.Move(1020,1000);app.Run({1});ExpectNear(app.runtime().soulCamera()->forward,front);
  app.runtime().setSoulMode(false);input.Move(1040,1000);app.Run({1});
  EXPECT_EQ(scene->camera().get<Jade::Transform>().rotation,gamePose.rotation);
}

TEST(CameraMotion, InvalidUpdatesAreAtomicAndGeneralMotionStillAllowsRoll)
{
  Jade::Transform pose;pose.position={1,2,3};const auto initial=pose;
  const auto nan=std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(Jade::CameraMotion::look(pose,nan,0),std::invalid_argument);
  EXPECT_THROW(Jade::CameraMotion::move(pose,{1,0,0},std::numeric_limits<float>::infinity()),std::invalid_argument);
  EXPECT_THROW(Jade::CameraMotion::pan(pose,{},nan,0,100),std::invalid_argument);
  EXPECT_EQ(pose.position,initial.position);EXPECT_EQ(pose.rotation,initial.rotation);
  Jade::Motion::roll(pose,180);EXPECT_LT(Jade::Motion::getUp(pose).y,0);
  Jade::CameraMotion::constrain(pose);EXPECT_GT(Jade::Motion::getUp(pose).y,0);
}
