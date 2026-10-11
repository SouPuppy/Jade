#include <Jade/Renderer/RayTracing/RayTracing.hpp>
#include "TestScene.hpp"
#include <Jade/Application.hpp>
#include <Jade/Events/Keyboard.hpp>
#include <Jade/Graphics/Motion.hpp>
#include "internal/Math/Matrix4.hpp"

#include "internal/Events/EventDispatcher.hpp"
#include "internal/Input/InputState.hpp"

#include <gtest/gtest.h>

#include <GLFW/glfw3.h>

#include <cmath>
#include <memory>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

Jade::EngineConfig HiddenInputApplication()
{
  Jade::EngineConfig config;
  config.window.visible = false;
  config.window.width = 160;
  config.window.height = 120;
  config.logPerformance = false;
  return config;
}

// Exercise the installed adapter directly; no OS message injection is needed.
struct KeyboardAdapter {
  GLFWwindow *window = glfwGetCurrentContext();
  GLFWkeyfun key = nullptr;
  GLFWwindowfocusfun focus = nullptr;
  GLFWcursorposfun cursor = nullptr;
  GLFWmousebuttonfun button = nullptr;
  GLFWscrollfun scroll = nullptr;

  KeyboardAdapter()
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
    if (!key || !focus || !cursor || !button || !scroll) {
      throw std::runtime_error("Jade input callbacks are missing.");
    }
    focus(window, GLFW_TRUE);
  }

  void Send(int code, int action, int modifiers = 0) const { key(window, code, 0, action, modifiers); }

  void Focus(bool focused) const { focus(window, focused ? GLFW_TRUE : GLFW_FALSE); }
  void Cursor(double x, double y) const { cursor(window, x, y); }
  void Button(int code, int action, int modifiers = 0) const { button(window, code, action, modifiers); }
  void Scroll(double dx, double dy) const { scroll(window, dx, dy); }
};

} // namespace

TEST(KeyBinding, ParsesNamesAndExactModifierCombinations)
{
  using Jade::Key;
  using Jade::Modifiers;
  static_assert(Jade::KeyHeld<"w">::chord == Jade::KeyChord{Key::W, Modifiers::None});
  static_assert(Jade::KeyHeld<"Space">::chord.key == Key::Space);
  static_assert(Jade::KeyHeld<"Up">::chord.key == Key::Up);
  static_assert(Jade::KeyHeld<"Down">::chord.key == Key::Down);
  static_assert(Jade::KeyHeld<"Left">::chord.key == Key::Left);
  static_assert(Jade::KeyHeld<"Right">::chord.key == Key::Right);
  static_assert(Jade::KeyHeld<"C-S-w">::chord ==
                Jade::KeyChord{Key::W, Modifiers::Control | Modifiers::Shift});
  EXPECT_EQ(Jade::KeyPressed<"s-c-W">::chord, Jade::KeyHeld<"C-S-w">::chord);
  EXPECT_EQ(Jade::KeyHeld<"F12">::chord.key, Key::F12);
  EXPECT_EQ(Jade::KeyHeld<"0">::chord.key, Key::D0);
  for (const char *invalid : {"", "C-", "S-", "C-C-w", "Q-w", "F0", "F13", "Spcae"}) {
    EXPECT_EQ(Jade::Detail::ParseKeyChord(invalid).key, Key::Unknown) << invalid;
  }
}

TEST(InputState, PreservesQuickTapsAndIgnoresRepeatsAndUnknownKeys)
{
  Jade::Detail::InputState input;
  input.OnKey(Jade::Key::W, Jade::KeyAction::Pressed, Jade::Modifiers::Control);
  input.OnKey(Jade::Key::W, Jade::KeyAction::Pressed, Jade::Modifiers::Control);
  input.OnKey(Jade::Key::W, Jade::KeyAction::Held, Jade::Modifiers::Control);
  EXPECT_TRUE(input.IsDown(Jade::Key::W));
  // Modifiers held before focus are still known from the key callback.
  EXPECT_EQ(input.CurrentModifiers(), Jade::Modifiers::Control);
  input.OnKey(Jade::Key::W, Jade::KeyAction::Released, Jade::Modifiers::Control);
  input.OnKey(Jade::Key::Unknown, Jade::KeyAction::Pressed, Jade::Modifiers::None);
  input.OnKey(Jade::Key::Count, Jade::KeyAction::Pressed, Jade::Modifiers::None);
  const auto events = input.TakeEvents();
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(std::get<Jade::Detail::KeyTransition>(events[0]).action, Jade::KeyAction::Pressed);
  EXPECT_EQ(std::get<Jade::Detail::KeyTransition>(events[1]).action, Jade::KeyAction::Released);
  EXPECT_EQ(std::get<Jade::Detail::KeyTransition>(events[0]).event.modifiers, Jade::Modifiers::Control);
  EXPECT_FALSE(input.IsDown(Jade::Key::W));
  EXPECT_TRUE(input.TakeEvents().empty());
}

TEST(InputState, FocusLossReleasesAllKeysAndClearsHeldState)
{
  Jade::Detail::InputState input;
  input.OnKey(Jade::Key::LeftControl, Jade::KeyAction::Pressed, Jade::Modifiers::Control);
  input.OnKey(Jade::Key::W, Jade::KeyAction::Pressed, Jade::Modifiers::Control);
  input.TakeEvents();
  input.OnFocus(false);
  EXPECT_FALSE(input.IsDown(Jade::Key::W));
  EXPECT_FALSE(input.IsDown(Jade::Key::LeftControl));
  EXPECT_EQ(input.CurrentModifiers(), Jade::Modifiers::None);
  const auto releases = input.TakeEvents();
  ASSERT_EQ(releases.size(), 2u);
  for (const auto &event : releases) {
    const auto &release = std::get<Jade::Detail::KeyTransition>(event);
    EXPECT_EQ(release.action, Jade::KeyAction::Released);
    EXPECT_EQ(release.event.modifiers, Jade::Modifiers::Control);
  }
  input.OnKey(Jade::Key::W, Jade::KeyAction::Released, Jade::Modifiers::None);
  input.OnKey(Jade::Key::W, Jade::KeyAction::Pressed, Jade::Modifiers::None);
  EXPECT_TRUE(input.TakeEvents().empty());
  input.OnFocus(true);
  input.OnKey(Jade::Key::W, Jade::KeyAction::Pressed, Jade::Modifiers::None);
  EXPECT_TRUE(input.IsDown(Jade::Key::W));
  EXPECT_EQ(input.TakeEvents().size(), 1u);
}

TEST(Events, ExactModifiersPreserveOrderAndStopPropagation)
{
  Jade::Detail::EventDispatcher events;
  std::vector<int> calls;
  events.Connect(Jade::KeyAction::Held, {Jade::Key::W, Jade::Modifiers::None}, [&](const auto &) {
    calls.push_back(0);
    return Jade::EventFlow::Continue;
  });
  events.Connect(Jade::KeyAction::Held, Jade::KeyHeld<"C-w">::chord, [&, count = 0](const auto &) mutable {
    calls.push_back(++count);
    return Jade::EventFlow::Continue;
  });
  const auto stop = events.Connect(Jade::KeyAction::Held, Jade::KeyHeld<"C-w">::chord, [&](const auto &) {
    calls.push_back(10);
    return Jade::EventFlow::Stop;
  });
  events.Connect(Jade::KeyAction::Held, Jade::KeyHeld<"C-w">::chord, [&](const auto &) {
    calls.push_back(20);
    return Jade::EventFlow::Continue;
  });
  const Jade::KeyEvent ctrlW{Jade::Key::W, Jade::Modifiers::Control, 0.1f};
  EXPECT_EQ(events.Dispatch(Jade::KeyAction::Held, ctrlW), Jade::EventFlow::Stop);
  EXPECT_EQ(calls, (std::vector<int>{1, 10}));
  *stop = false;
  EXPECT_EQ(events.Dispatch(Jade::KeyAction::Held, ctrlW), Jade::EventFlow::Continue);
  EXPECT_EQ(calls, (std::vector<int>{1, 10, 2, 20}));
  EXPECT_EQ(events.Dispatch(Jade::KeyAction::Held,
                            {Jade::Key::W, Jade::Modifiers::Control | Jade::Modifiers::Shift, 0.1f}),
            Jade::EventFlow::Continue);
  EXPECT_EQ(calls.size(), 4u);
}

TEST(Events, RegistrationCancellationAndExceptionsAreSafeDuringDispatch)
{
  Jade::Detail::EventDispatcher events;
  std::vector<int> calls;
  std::shared_ptr<bool> cancelled;
  const auto first = events.Connect([&](const Jade::FrameUpdate &) {
    calls.push_back(1);
    *cancelled = false;
    events.Connect([&](const Jade::FrameUpdate &) {
      calls.push_back(3);
      return Jade::EventFlow::Continue;
    });
    return Jade::EventFlow::Continue;
  });
  cancelled = events.Connect([&](const Jade::FrameUpdate &) {
    calls.push_back(2);
    return Jade::EventFlow::Continue;
  });
  events.Dispatch(Jade::FrameUpdate{});
  EXPECT_EQ(calls, (std::vector<int>{1}));
  *first = false;
  events.Dispatch(Jade::FrameUpdate{});
  EXPECT_EQ(calls, (std::vector<int>{1, 3}));
  const auto throwing = events.Connect(
      [](const Jade::FrameUpdate &) -> Jade::EventFlow { throw std::runtime_error("callback failure"); });
  EXPECT_THROW(events.Dispatch(Jade::FrameUpdate{}), std::runtime_error);
  *throwing = false;
  EXPECT_NO_THROW(events.Dispatch(Jade::FrameUpdate{}));
  EXPECT_EQ(calls, (std::vector<int>{1, 3, 3, 3}));
}

TEST(Application, KeyboardAdapterDispatchesChordsAndFrameBasedMovement)
{
  Jade::Application app(HiddenInputApplication());
  MakeTestScene(app);
  KeyboardAdapter input;
  auto &camera = app.runtime().scene()->camera().get<Jade::Transform>();
  camera.position = {0, 0, 5};
  Jade::Motion::lookAt(camera, {0, 0, 0});
  int pressed = 0, released = 0, held = 0, both = 0, bare = 0;
  double moved = 0;
  app.runtime().scene()->on<Jade::KeyPressed<"C-w">>([&] { ++pressed; });
  app.runtime().scene()->on<Jade::KeyReleased<"C-w">>([&] { ++released; });
  app.runtime().scene()->on<Jade::KeyHeld<"w">>([&] { ++bare; });
  app.runtime().scene()->on<Jade::KeyHeld<"C-S-w">>([&] { ++both; });
  app.runtime().scene()->on<Jade::KeyHeld<"C-w">>([&](const auto &event) {
    ++held;
    EXPECT_TRUE(std::isfinite(event.dt));
    EXPECT_GE(event.dt, 0);
    const float distance = 3.0f * event.dt;
    camera.position = camera.position + Jade::Motion::getFront(camera) * distance;
    moved += distance;
  });
  app.runtime().scene()->on<Jade::FrameUpdate>([&](const auto &frame) {
    if (frame.frame == 1) {
      EXPECT_FLOAT_EQ(frame.dt, 0);
      input.Send(GLFW_KEY_W, GLFW_REPEAT, GLFW_MOD_CONTROL);
    } else if (frame.frame == 2) {
      input.Send(GLFW_KEY_LEFT_SHIFT, GLFW_PRESS, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT);
    } else if (frame.frame == 3) {
      input.Send(GLFW_KEY_LEFT_SHIFT, GLFW_RELEASE, GLFW_MOD_CONTROL);
    } else if (frame.frame == 4) {
      input.Send(GLFW_KEY_W, GLFW_RELEASE, GLFW_MOD_CONTROL);
      input.Send(GLFW_KEY_LEFT_CONTROL, GLFW_RELEASE);
    }
  });
  input.Send(GLFW_KEY_LEFT_CONTROL, GLFW_PRESS, GLFW_MOD_CONTROL);
  input.Send(GLFW_KEY_W, GLFW_PRESS, GLFW_MOD_CONTROL);
  EXPECT_NO_THROW(app.Run({5}));
  EXPECT_EQ(pressed, 1);
  EXPECT_EQ(released, 1);
  EXPECT_EQ(held, 3);
  EXPECT_EQ(both, 1);
  EXPECT_EQ(bare, 0);
  EXPECT_GT(moved, 0);
  EXPECT_NEAR(camera.position.z, 5.0 - moved, 1e-5);
  EXPECT_EQ(Jade::Motion::getFront(camera), (Jade::Vec3{0, 0, -1}));
}

TEST(Application, QuickTapsAndConsumedEscapeDispatchBeforeDrawing)
{
  Jade::Application app(HiddenInputApplication());
  MakeTestScene(app);
  KeyboardAdapter input;
  std::vector<int> order;
  app.runtime().scene()->on<Jade::KeyPressed<"w">>([&] { order.push_back(1); });
  app.runtime().scene()->on<Jade::KeyReleased<"w">>([&] { order.push_back(2); });
  app.runtime().scene()->on<Jade::KeyHeld<"w">>([&] { order.push_back(99); });
  app.runtime().scene()->on<Jade::KeyPressed<"Escape">>([&] {
    order.push_back(3);
    return Jade::EventFlow::Stop;
  });
  app.runtime().scene()->on<Jade::KeyPressed<"Escape">>([&] { order.push_back(99); });
  app.runtime().scene()->on<Jade::FrameUpdate>([&] { order.push_back(4); });
  input.Send(GLFW_KEY_W, GLFW_PRESS);
  input.Send(GLFW_KEY_W, GLFW_RELEASE);
  input.Send(GLFW_KEY_ESCAPE, GLFW_PRESS);
  EXPECT_NO_THROW(app.Run({1}));
  EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 4}));
  EXPECT_EQ(glfwWindowShouldClose(input.window), GLFW_FALSE);
}

TEST(Application, FocusLossStopsHeldCallbacksAndReleasesOnce)
{
  Jade::Application app(HiddenInputApplication());
  MakeTestScene(app);
  KeyboardAdapter input;
  int held = 0, released = 0;
  app.runtime().scene()->on<Jade::KeyHeld<"Space">>([&] { ++held; });
  app.runtime().scene()->on<Jade::KeyReleased<"Space">>([&] { ++released; });
  const auto loseFocus = app.runtime().scene()->on<Jade::FrameUpdate>([&](const auto &frame) {
    if (frame.frame == 1) {
      input.Focus(false);
      input.Send(GLFW_KEY_SPACE, GLFW_RELEASE);
    }
  });
  input.Send(GLFW_KEY_SPACE, GLFW_PRESS);
  app.Run({3});
  EXPECT_EQ(held, 1);
  EXPECT_EQ(released, 1);
  loseFocus.disconnect();
  input.Focus(true);
  input.Send(GLFW_KEY_SPACE, GLFW_PRESS);
  app.Run({1});
  EXPECT_EQ(held, 2);
}

TEST(Application, StopAndDisconnectionPermitLaterRunsAndHandlesExpire)
{
  Jade::EventConnection expired;
  {
    Jade::Application app(HiddenInputApplication());
    MakeTestScene(app);
    KeyboardAdapter input;
    int frames = 0;
    expired = app.runtime().scene()->on<Jade::FrameUpdate>([&] { ++frames; });
    const auto stopping = app.runtime().scene()->on<Jade::KeyPressed<"Escape">>([&] { app.stop(); });
    input.Send(GLFW_KEY_ESCAPE, GLFW_PRESS);
    EXPECT_NO_THROW(app.Run({5}));
    EXPECT_EQ(frames, 0);
    stopping.disconnect();
    EXPECT_FALSE(stopping.connected());
    EXPECT_TRUE(expired.connected());
    input.Send(GLFW_KEY_ESCAPE, GLFW_RELEASE);
    EXPECT_NO_THROW(app.Run({1}));
    EXPECT_EQ(frames, 1);
  }
  EXPECT_FALSE(expired.connected());
  EXPECT_NO_THROW(expired.disconnect());
}

TEST(Application, CallbackExceptionsAndNestedRunsLeaveTheLoopRecoverable)
{
  Jade::Application app(HiddenInputApplication());
  MakeTestScene(app);
  bool fail = true;
  const auto throwing = app.runtime().scene()->on<Jade::FrameUpdate>([&] {
    if (fail) {
      fail = false;
      throw std::runtime_error("callback failure");
    }
  });
  EXPECT_THROW(app.Run({1}), std::runtime_error);
  EXPECT_NO_THROW(app.Run({1}));
  throwing.disconnect();
  int count = 0;
  app.runtime().scene()->on<Jade::FrameUpdate>([&, counter = std::make_unique<int>(0)]() mutable {
    EXPECT_THROW(app.Run({1}), std::logic_error);
    count = ++*counter;
  });
  EXPECT_NO_THROW(app.Run({2}));
  EXPECT_EQ(count, 2);
}

TEST(Camera, MovementKeepsOrientationAndAxesMatchTheView)
{
  Jade::Transform camera;
  camera.position = {0, 0, 5};
  Jade::Motion::lookAt(camera, {0, 0, 0});
  EXPECT_EQ(Jade::Motion::getFront(camera), (Jade::Vec3{0, 0, -1}));
  EXPECT_EQ(Jade::Motion::getRight(camera), (Jade::Vec3{1, 0, 0}));
  EXPECT_EQ(Jade::Motion::getUp(camera), (Jade::Vec3{0, 1, 0}));
  // Crossing the old target is valid; position changes no longer retarget the eye.
  camera.position = camera.position + Jade::Motion::getFront(camera) * 6.0f;
  EXPECT_EQ(camera.position, (Jade::Vec3{0, 0, -1}));
  EXPECT_EQ(Jade::Motion::getFront(camera), (Jade::Vec3{0, 0, -1}));
  camera.rotation = Jade::Quaternion::angleAxis({0, 1, 0}, 90);
  EXPECT_NEAR(Jade::Motion::getFront(camera).x, -1, 1e-6);
  EXPECT_NEAR(Jade::Motion::getFront(camera).z, 0, 1e-6);
  EXPECT_NEAR(Jade::dot(Jade::Motion::getFront(camera), Jade::Motion::getRight(camera)), 0, 1e-6);
  EXPECT_NEAR(Jade::dot(Jade::Motion::getFront(camera), Jade::Motion::getFront(camera)), 1, 1e-6);
}

TEST(InputState, MouseDeltasKeepInputOrderAndResetAfterFocusLoss)
{
  Jade::Detail::InputState input;
  input.OnCursor(100, 200);
  input.OnKey(Jade::Key::W, Jade::KeyAction::Pressed, Jade::Modifiers::None);
  input.OnCursor(120, 190);
  const auto events = input.TakeEvents();
  ASSERT_EQ(events.size(), 3u);
  const auto first = std::get<Jade::MouseMoved>(events[0]);
  EXPECT_DOUBLE_EQ(first.dx, 0);
  EXPECT_DOUBLE_EQ(first.dy, 0);
  EXPECT_EQ(std::get<Jade::Detail::KeyTransition>(events[1]).event.key, Jade::Key::W);
  const auto next = std::get<Jade::MouseMoved>(events[2]);
  EXPECT_DOUBLE_EQ(next.dx, 20);
  EXPECT_DOUBLE_EQ(next.dy, -10);
  input.OnFocus(false);
  input.TakeEvents();
  input.OnCursor(500, 600);
  EXPECT_TRUE(input.TakeEvents().empty());
  input.OnFocus(true);
  input.OnCursor(900, 1000);
  const auto resumed = std::get<Jade::MouseMoved>(input.TakeEvents().front());
  EXPECT_DOUBLE_EQ(resumed.dx, 0);
  EXPECT_DOUBLE_EQ(resumed.dy, 0);
  input.OnCursor(std::numeric_limits<double>::infinity(), 0);
  EXPECT_TRUE(input.TakeEvents().empty());
}

TEST(Application, MouseAdapterDispatchesInOrderAndMotionTurnsTheCamera)
{
  Jade::Application app(HiddenInputApplication());
  MakeTestScene(app);
  KeyboardAdapter input;
  auto &camera = app.runtime().scene()->camera().get<Jade::Transform>();
  camera.position = {0, 0, 5};
  Jade::Motion::lookAt(camera, {0, 0, 0});
  std::vector<int> order;
  app.runtime().scene()->on<Jade::KeyPressed<"w">>([&] { order.push_back(1); });
  const auto mouse = app.runtime().scene()->on<Jade::MouseMoved>([&](const auto &event) {
    order.push_back(2);
    Jade::Motion::yaw(camera, event.dx);
    Jade::Motion::pitch(camera, -event.dy);
    return Jade::EventFlow::Stop;
  });
  app.runtime().scene()->on<Jade::MouseMoved>([&] { order.push_back(99); });
  app.runtime().scene()->on<Jade::KeyReleased<"w">>([&] { order.push_back(3); });
  input.Cursor(100, 200);
  input.Send(GLFW_KEY_W, GLFW_PRESS);
  input.Cursor(190, 200);
  input.Send(GLFW_KEY_W, GLFW_RELEASE);
  EXPECT_NO_THROW(app.Run({1}));
  EXPECT_EQ(order, (std::vector<int>{2, 1, 2, 3}));
  EXPECT_NEAR(Jade::Motion::getFront(camera).x, 1, 1e-6);
  EXPECT_NEAR(Jade::Motion::getFront(camera).z, 0, 1e-6);
  mouse.disconnect();
  input.Cursor(200, 200);
  EXPECT_NO_THROW(app.Run({1}));
  EXPECT_EQ(order.back(), 99);
}

TEST(Motion, TranslatesAlongCameraAxesAndWorldVerticalWithoutChangingOrientation)
{
  Jade::Transform camera;
  camera.position = {0, 0, 5};
  Jade::Motion::lookAt(camera, {0, 0, 0});
  Jade::Motion::forward(camera, 2);
  EXPECT_EQ(camera.position, (Jade::Vec3{0, 0, 3}));
  Jade::Motion::backward(camera, 2);
  Jade::Motion::right(camera, 3);
  EXPECT_EQ(camera.position, (Jade::Vec3{3, 0, 5}));
  Jade::Motion::left(camera, 3);
  Jade::Motion::up(camera, 2);
  EXPECT_EQ(camera.position, (Jade::Vec3{0, 2, 5}));
  Jade::Motion::down(camera, 2);
  EXPECT_EQ(camera.position, (Jade::Vec3{0, 0, 5}));
  EXPECT_EQ(Jade::Motion::getFront(camera), (Jade::Vec3{0, 0, -1}));
  Jade::Motion::yaw(camera, 90);
  Jade::Motion::forward(camera);
  EXPECT_NEAR(camera.position.x, 0.1f, 1e-6);
  EXPECT_NEAR(camera.position.z, 5, 1e-6);
  Jade::Motion::pitch(camera, 30);
  const auto before = camera.position;
  Jade::Motion::up(camera, 1);
  EXPECT_NEAR(camera.position.x, before.x, 1e-6);
  EXPECT_NEAR(camera.position.y, before.y + 1, 1e-6);
  EXPECT_NEAR(camera.position.z, before.z, 1e-6);
}

TEST(Motion, WrapsAnglesAllowsFlipsAndRejectsNonfiniteUpdatesAtomically)
{
  Jade::Transform camera;
  Jade::Motion::yaw(camera, 450);
  EXPECT_NEAR(Jade::Motion::getFront(camera).x, 1, 1e-6);
  camera.rotation = {};
  Jade::Motion::pitch(camera, 180);
  EXPECT_NEAR(Jade::Motion::getFront(camera).z, 1, 1e-6);
  EXPECT_NEAR(Jade::Motion::getUp(camera).y, -1, 1e-6);
  const auto rotation = camera.rotation;
  const auto position = camera.position;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(Jade::Motion::yaw(camera, nan), std::invalid_argument);
  EXPECT_THROW(Jade::Motion::pitch(camera, std::numeric_limits<double>::infinity()), std::invalid_argument);
  EXPECT_THROW(Jade::Motion::roll(camera, nan), std::invalid_argument);
  EXPECT_THROW(Jade::Motion::forward(camera, std::numeric_limits<float>::infinity()), std::invalid_argument);
  EXPECT_EQ(camera.rotation, rotation);
  EXPECT_EQ(camera.position, position);
}

TEST(Motion, RollBanksTheCameraAndRotatesItsMovementAxes)
{
  Jade::Transform camera;
  camera.position = {0, 0, 5};
  Jade::Motion::lookAt(camera, {0, 0, 0});
  Jade::Motion::roll(camera, 450);
  EXPECT_EQ(Jade::Motion::getFront(camera), (Jade::Vec3{0, 0, -1}));
  EXPECT_NEAR(Jade::Motion::getRight(camera).x, 0, 1e-6);
  EXPECT_NEAR(Jade::Motion::getRight(camera).y, -1, 1e-6);
  EXPECT_NEAR(Jade::Motion::getUp(camera).x, 1, 1e-6);
  EXPECT_NEAR(Jade::Motion::getUp(camera).y, 0, 1e-6);
  Jade::Motion::right(camera, 2);
  EXPECT_NEAR(camera.position.x, 0, 1e-6);
  EXPECT_NEAR(camera.position.y, -2, 1e-6);
  EXPECT_NEAR(camera.position.z, 5, 1e-6);
  Jade::Motion::up(camera, 2);
  EXPECT_NEAR(camera.position.y, 0, 1e-6);
  Jade::Motion::roll(camera, -450);
  EXPECT_EQ(Jade::Motion::getRight(camera), (Jade::Vec3{1, 0, 0}));
  Jade::Motion::yaw(camera, 40);
  Jade::Motion::pitch(camera, 25);
  const auto front = Jade::Motion::getFront(camera);
  const auto right = Jade::Motion::getRight(camera);
  const auto up = Jade::Motion::getUp(camera);
  Jade::Motion::roll(camera, 90);
  EXPECT_NEAR(Jade::Motion::getFront(camera).x, front.x, 1e-6);
  EXPECT_NEAR(Jade::Motion::getFront(camera).y, front.y, 1e-6);
  EXPECT_NEAR(Jade::Motion::getFront(camera).z, front.z, 1e-6);
  EXPECT_NEAR(Jade::Motion::getRight(camera).x, -up.x, 1e-6);
  EXPECT_NEAR(Jade::Motion::getRight(camera).y, -up.y, 1e-6);
  EXPECT_NEAR(Jade::Motion::getRight(camera).z, -up.z, 1e-6);
  EXPECT_NEAR(Jade::Motion::getUp(camera).x, right.x, 1e-6);
  EXPECT_NEAR(Jade::Motion::getUp(camera).y, right.y, 1e-6);
  EXPECT_NEAR(Jade::Motion::getUp(camera).z, right.z, 1e-6);
}

TEST(Motion, MouseYawAndPitchUseLocalAxesAfterRoll)
{
  Jade::Transform pose;
  Jade::Motion::roll(pose, 90);
  Jade::Motion::yaw(pose, 90);
  auto front = Jade::Motion::getFront(pose);
  EXPECT_NEAR(front.x, 0, 1e-6);
  EXPECT_NEAR(front.y, -1, 1e-6);
  EXPECT_NEAR(front.z, 0, 1e-6);
  pose.rotation = {};
  Jade::Motion::roll(pose, 90);
  Jade::Motion::pitch(pose, 90);
  front = Jade::Motion::getFront(pose);
  EXPECT_NEAR(front.x, 1, 1e-6);
  EXPECT_NEAR(front.y, 0, 1e-6);
  EXPECT_NEAR(front.z, 0, 1e-6);
  // Continuous control remains orthonormal without Euler pole clamping.
  for (int index = 0; index < 10000; ++index) {
    Jade::Motion::yaw(pose, 0.3);
    Jade::Motion::pitch(pose, -0.2);
    Jade::Motion::roll(pose, 0.1);
  }
  front = Jade::Motion::getFront(pose);
  const auto right = Jade::Motion::getRight(pose), up = Jade::Motion::getUp(pose);
  EXPECT_NEAR(Jade::dot(front, front), 1, 1e-6);
  EXPECT_NEAR(Jade::dot(right, right), 1, 1e-6);
  EXPECT_NEAR(Jade::dot(up, up), 1, 1e-6);
  EXPECT_NEAR(Jade::dot(front, right), 0, 1e-6);
  EXPECT_NEAR(Jade::dot(front, up), 0, 1e-6);
  EXPECT_NEAR(Jade::dot(right, up), 0, 1e-6);
}

TEST(MouseBinding, ParsesButtonNamesAndRejectsUnknownBindings)
{
  static_assert(Jade::MousePressed<"Left">::binding == Jade::MouseButton::Left);
  static_assert(Jade::MouseReleased<"right">::binding == Jade::MouseButton::Right);
  static_assert(Jade::MouseHeld<"MIDDLE">::binding == Jade::MouseButton::Middle);
  EXPECT_EQ(Jade::MouseHeld<"Button8">::binding, Jade::MouseButton::Button8);
  for (const char *invalid : {"", "w", "Lfet", "C-Left", "Button9"}) {
    EXPECT_EQ(Jade::Detail::ParseMouseButton(invalid), Jade::MouseButton::Unknown);
  }
}

TEST(InputState, MouseButtonsAndFractionalScrollShareTheOrderedQueue)
{
  Jade::Detail::InputState input;
  input.OnMouseButton(Jade::MouseButton::Left, Jade::MouseAction::Pressed, Jade::Modifiers::Shift);
  input.OnMouseButton(Jade::MouseButton::Left, Jade::MouseAction::Pressed, Jade::Modifiers::Shift);
  input.OnMouseButton(Jade::MouseButton::Unknown, Jade::MouseAction::Pressed, Jade::Modifiers::None);
  input.OnMouseButton(Jade::MouseButton::Count, Jade::MouseAction::Pressed, Jade::Modifiers::None);
  input.OnMouseButton(Jade::MouseButton::Left, Jade::MouseAction::Held, Jade::Modifiers::Shift);
  input.OnCursor(10, 20);
  input.OnScroll(0.25, -0.5);
  input.OnMouseButton(Jade::MouseButton::Left, Jade::MouseAction::Released, Jade::Modifiers::None);
  input.OnScroll(0, 0);
  input.OnScroll(std::numeric_limits<double>::infinity(), 1);
  input.OnScroll(1, std::numeric_limits<double>::quiet_NaN());
  const auto events = input.TakeEvents();
  ASSERT_EQ(events.size(), 4u);
  const auto &press = std::get<Jade::Detail::MouseTransition>(events[0]);
  EXPECT_EQ(press.action, Jade::MouseAction::Pressed);
  EXPECT_EQ(press.event.button, Jade::MouseButton::Left);
  EXPECT_EQ(press.event.modifiers, Jade::Modifiers::Shift);
  EXPECT_FLOAT_EQ(press.event.dt, 0);
  EXPECT_TRUE(std::holds_alternative<Jade::MouseMoved>(events[1]));
  const auto &scroll = std::get<Jade::MouseScrolled>(events[2]);
  EXPECT_DOUBLE_EQ(scroll.dx, 0.25);
  EXPECT_DOUBLE_EQ(scroll.dy, -0.5);
  EXPECT_EQ(std::get<Jade::Detail::MouseTransition>(events[3]).action, Jade::MouseAction::Released);
  EXPECT_FALSE(input.IsDown(Jade::MouseButton::Left));
  EXPECT_FALSE(input.IsDown(Jade::MouseButton::Unknown));
  EXPECT_FALSE(input.IsDown(Jade::MouseButton::Count));
}

TEST(Application, LeftDragAndScrollKeepArrivalOrderEvenForATapBetweenFrames)
{
  Jade::Application app(HiddenInputApplication());
  MakeTestScene(app);
  KeyboardAdapter input;
  auto &lens = app.runtime().scene()->camera().get<Jade::Camera>();
  auto &camera = app.runtime().scene()->camera().get<Jade::Transform>();
  camera.position = {0, 0, 5};
  Jade::Motion::lookAt(camera, {0, 0, 0});
  bool dragging = false;
  int held = 0;
  std::vector<char> order;
  app.runtime().scene()->on<Jade::MousePressed<"Left">>([&] {
    order.push_back('p');
    dragging = true;
    return Jade::EventFlow::Stop;
  });
  app.runtime().scene()->on<Jade::MousePressed<"Left">>([&] { order.push_back('!'); });
  app.runtime().scene()->on<Jade::MousePressed<"Right">>([&] { order.push_back('!'); });
  app.runtime().scene()->on<Jade::MouseReleased<"Left">>([&](const auto &event) {
    EXPECT_FLOAT_EQ(event.dt, 0);
    order.push_back('r');
    dragging = false;
  });
  app.runtime().scene()->on<Jade::MouseHeld<"Left">>([&] { ++held; });
  app.runtime().scene()->on<Jade::MouseMoved>([&](const auto &mouse) {
    order.push_back('m');
    if (dragging) {
      Jade::Motion::yaw(camera, mouse.dx);
      Jade::Motion::pitch(camera, -mouse.dy);
    }
  });
  const auto scrolling = app.runtime().scene()->on<Jade::MouseScrolled>([&](const auto &mouse) {
    order.push_back('s');
    Jade::Motion::zoom(lens, mouse.dy);
    return Jade::EventFlow::Stop;
  });
  app.runtime().scene()->on<Jade::MouseScrolled>([&] { order.push_back('!'); });
  input.Cursor(100, 100);
  input.Cursor(120, 100);
  input.Button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, GLFW_MOD_CONTROL);
  input.Cursor(140, 90);
  input.Scroll(0.25, 0.5);
  input.Button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
  input.Cursor(200, 30);
  input.Scroll(-0.75, -0.5);
  app.Run({1});
  EXPECT_EQ(order, (std::vector<char>{'m', 'm', 'p', 'm', 's', 'r', 'm', 's'}));
  EXPECT_FALSE(dragging);
  EXPECT_EQ(held, 0);
  Jade::Transform expected;
  Jade::Motion::yaw(expected, 20);
  Jade::Motion::pitch(expected, 10);
  EXPECT_EQ(camera.rotation, expected.rotation);
  EXPECT_NEAR(lens.fovDegrees, 60, 1e-5);
  EXPECT_EQ(camera.position, (Jade::Vec3{0, 0, 5}));
  scrolling.disconnect();
  input.Scroll(0, 1);
  app.Run({1});
  EXPECT_EQ(order.back(), '!');
}

TEST(Application, MouseHeldSharesFrameDtAndStopsAfterRelease)
{
  Jade::Application app(HiddenInputApplication());
  MakeTestScene(app);
  KeyboardAdapter input;
  std::vector<float> mouseDt;
  std::vector<float> frameDt;
  int pressed = 0, released = 0, wrongButton = 0;
  app.runtime().scene()->on<Jade::MousePressed<"Right">>([&] { ++pressed; });
  app.runtime().scene()->on<Jade::MouseReleased<"Right">>([&] { ++released; });
  app.runtime().scene()->on<Jade::MouseHeld<"Left">>([&] { ++wrongButton; });
  app.runtime().scene()->on<Jade::MouseHeld<"Right">>([&](const auto &event) {
    EXPECT_EQ(event.button, Jade::MouseButton::Right);
    EXPECT_TRUE(std::isfinite(event.dt));
    EXPECT_GE(event.dt, 0);
    mouseDt.push_back(event.dt);
  });
  app.runtime().scene()->on<Jade::FrameUpdate>([&](const auto &frame) {
    frameDt.push_back(frame.dt);
    if (frame.frame == 2) {
      input.Button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
    }
  });
  input.Button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
  input.Button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_REPEAT);
  app.Run({3});
  EXPECT_EQ(pressed, 1);
  EXPECT_EQ(released, 1);
  EXPECT_EQ(wrongButton, 0);
  ASSERT_EQ(mouseDt.size(), 2u);
  ASSERT_EQ(frameDt.size(), 3u);
  EXPECT_FLOAT_EQ(mouseDt[0], 0);
  EXPECT_GT(mouseDt[1], 0);
  EXPECT_FLOAT_EQ(mouseDt[0], frameDt[0]);
  EXPECT_FLOAT_EQ(mouseDt[1], frameDt[1]);
}

TEST(Application, FocusLossReleasesButtonsAndEndsDragBeforeLaterMotion)
{
  Jade::Application app(HiddenInputApplication());
  MakeTestScene(app);
  KeyboardAdapter input;
  bool dragging = false;
  int held = 0, released = 0, dragged = 0, scrolled = 0;
  app.runtime().scene()->on<Jade::MousePressed<"Left">>([&] { dragging = true; });
  app.runtime().scene()->on<Jade::MouseReleased<"Left">>([&] {
    dragging = false;
    ++released;
  });
  app.runtime().scene()->on<Jade::MouseHeld<"Left">>([&] { ++held; });
  app.runtime().scene()->on<Jade::MouseMoved>([&] {
    if (dragging) {
      ++dragged;
    }
  });
  app.runtime().scene()->on<Jade::MouseScrolled>([&] { ++scrolled; });
  app.runtime().scene()->on<Jade::FrameUpdate>([&](const auto &frame) {
    if (frame.frame == 1) {
      input.Focus(false);
      input.Button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
      input.Button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
      input.Scroll(0, 1);
      input.Cursor(100, 100);
      input.Focus(true);
      input.Cursor(500, 500);
    }
  });
  input.Button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
  app.Run({3});
  EXPECT_EQ(held, 1);
  EXPECT_EQ(released, 1);
  EXPECT_EQ(dragged, 0);
  EXPECT_EQ(scrolled, 0);
  EXPECT_FALSE(dragging);
}

TEST(Motion, ZoomChangesBothProjectionsWithoutChangingPoseOrClipPlanes)
{
  Jade::Camera lens;
  Jade::Transform camera;
  lens.projection = Jade::Projection::Perspective;
  lens.fovDegrees = 60;
  lens.nearPlane = 0.25f;
  lens.farPlane = 250;
  const auto position = camera.position;
  const auto front = Jade::Motion::getFront(camera);
  Jade::Motion::zoom(lens, 1);
  EXPECT_LT(lens.fovDegrees, 60);
  Jade::Motion::zoom(lens, -1);
  EXPECT_NEAR(lens.fovDegrees, 60, 1e-5);
  EXPECT_EQ(lens.projection, Jade::Projection::Perspective);
  EXPECT_FLOAT_EQ(lens.nearPlane, 0.25f);
  EXPECT_FLOAT_EQ(lens.farPlane, 250);
  lens.projection = Jade::Projection::Orthographic;
  lens.viewHeight = 6;
  lens.nearPlane = 0.5f;
  lens.farPlane = 500;
  Jade::Motion::zoom(lens, 0.5);
  EXPECT_LT(lens.viewHeight, 6);
  Jade::Motion::zoom(lens, -0.5);
  EXPECT_NEAR(lens.viewHeight, 6, 1e-5);
  EXPECT_EQ(lens.projection, Jade::Projection::Orthographic);
  EXPECT_FLOAT_EQ(lens.nearPlane, 0.5f);
  EXPECT_FLOAT_EQ(lens.farPlane, 500);
  EXPECT_EQ(camera.position, position);
  EXPECT_EQ(Jade::Motion::getFront(camera), front);
}

TEST(Motion, ZoomStaysValidAtExtremesAndRejectsNonfiniteOffsets)
{
  Jade::Camera lens;
  Jade::Transform camera;
  for (const auto projection : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
    if (projection == Jade::Projection::Perspective) {
      lens.projection = Jade::Projection::Perspective;
    } else {
      lens.projection = Jade::Projection::Orthographic;
    }
    Jade::Motion::zoom(lens, std::numeric_limits<double>::max());
    const float zoomedIn = projection == Jade::Projection::Perspective ? lens.fovDegrees : lens.viewHeight;
    EXPECT_GT(zoomedIn, 0);
    Jade::Motion::zoom(lens, -std::numeric_limits<double>::max());
    const float zoomedOut = projection == Jade::Projection::Perspective ? lens.fovDegrees : lens.viewHeight;
    EXPECT_TRUE(std::isfinite(zoomedOut));
    EXPECT_GT(zoomedOut, zoomedIn);
    if (projection == Jade::Projection::Perspective) {
      EXPECT_LT(zoomedOut, 180);
    }
    EXPECT_THROW(Jade::Motion::zoom(lens, std::numeric_limits<double>::infinity()), std::invalid_argument);
    EXPECT_THROW(Jade::Motion::zoom(lens, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
    Jade::Motion::zoom(lens, 0);
    EXPECT_FLOAT_EQ(projection == Jade::Projection::Perspective ? lens.fovDegrees : lens.viewHeight,
                    zoomedOut);
    EXPECT_EQ(lens.projection, projection);
  }
}

TEST(Application, OPressSwitchesProjectionOnceAndScrollUsesTheActiveMode)
{
  Jade::Application app(HiddenInputApplication());
  MakeTestScene(app);
  KeyboardAdapter input;
  auto &lens = app.runtime().scene()->camera().get<Jade::Camera>();
  auto &camera = app.runtime().scene()->camera().get<Jade::Transform>();
  lens.projection = Jade::Projection::Orthographic;
  lens.viewHeight = 8;
  lens.nearPlane = 0.25f;
  lens.farPlane = 250;
  lens.projection = Jade::Projection::Perspective;
  lens.fovDegrees = 60;
  lens.nearPlane = 0.25f;
  lens.farPlane = 250;
  const auto position = camera.position;
  const auto front = Jade::Motion::getFront(camera);
  int switches = 0;
  app.runtime().scene()->on<Jade::KeyPressed<"o">>([&] {
    lens.projection = lens.projection == Jade::Projection::Perspective ? Jade::Projection::Orthographic
                                                                       : Jade::Projection::Perspective;
    ++switches;
  });
  app.runtime().scene()->on<Jade::MouseScrolled>([&](const auto &mouse) { Jade::Motion::zoom(lens, mouse.dy); });
  input.Scroll(0, 1);
  input.Send(GLFW_KEY_O, GLFW_PRESS);
  input.Send(GLFW_KEY_O, GLFW_REPEAT);
  input.Scroll(0, 1);
  app.Run({2});
  EXPECT_EQ(switches, 1);
  EXPECT_EQ(lens.projection, Jade::Projection::Orthographic);
  EXPECT_LT(lens.fovDegrees, 60);
  EXPECT_LT(lens.viewHeight, 8);
  const auto fov = lens.fovDegrees;
  const auto height = lens.viewHeight;
  input.Send(GLFW_KEY_O, GLFW_RELEASE);
  input.Send(GLFW_KEY_O, GLFW_PRESS);
  app.Run({1});
  EXPECT_EQ(switches, 2);
  EXPECT_EQ(lens.projection, Jade::Projection::Perspective);
  EXPECT_FLOAT_EQ(lens.fovDegrees, fov);
  EXPECT_FLOAT_EQ(lens.viewHeight, height);
  EXPECT_FLOAT_EQ(lens.nearPlane, 0.25f);
  EXPECT_FLOAT_EQ(lens.farPlane, 250);
  EXPECT_EQ(camera.position, position);
  EXPECT_EQ(Jade::Motion::getFront(camera), front);
  lens.projection = static_cast<Jade::Projection>(100);
  EXPECT_THROW(Jade::Detail::ProjectionMatrix(lens, 1), std::invalid_argument);
  lens.projection = Jade::Projection::Perspective;
  EXPECT_EQ(lens.projection, Jade::Projection::Perspective);
}

TEST(Application, AxisGizmoDefaultsOffAndF3TogglesOncePerUnconsumedPress)
{
  EXPECT_FALSE(Jade::EngineConfig{}.debug.axes);
  EXPECT_FALSE(Jade::EngineConfig{}.debug.aabbs);
  for (const bool enabled : {true, false}) {
    auto config = HiddenInputApplication();
    config.debug.shortcuts = true;
    config.debug.axes = enabled;
    Jade::Application app(config);
    MakeTestScene(app);
    KeyboardAdapter input;
    EXPECT_EQ(app.runtime().axesEnabled(), enabled);
    input.Send(GLFW_KEY_F3, GLFW_PRESS);
    input.Send(GLFW_KEY_F3, GLFW_REPEAT);
    app.Run({2});
    EXPECT_EQ(app.runtime().axesEnabled(), !enabled);
    EXPECT_EQ(app.runtime().aabbsEnabled(), !enabled);
    input.Send(GLFW_KEY_F3, GLFW_RELEASE);
    input.Send(GLFW_KEY_F3, GLFW_PRESS);
    app.Run({1});
    EXPECT_EQ(app.runtime().axesEnabled(), enabled);
    EXPECT_EQ(app.runtime().aabbsEnabled(), enabled);
    const auto consumed = app.runtime().scene()->on<Jade::KeyPressed<"F3">>([] { return Jade::EventFlow::Stop; });
    input.Send(GLFW_KEY_F3, GLFW_RELEASE);
    input.Send(GLFW_KEY_F3, GLFW_PRESS);
    app.Run({1});
    EXPECT_EQ(app.runtime().axesEnabled(), enabled);
    consumed.disconnect();
    input.Send(GLFW_KEY_F3, GLFW_RELEASE);
    input.Send(GLFW_KEY_F3, GLFW_PRESS, GLFW_MOD_SHIFT);
    app.Run({1});
    EXPECT_EQ(app.runtime().axesEnabled(), enabled);
    app.runtime().setAxes(false);
    app.Run({1});
    EXPECT_FALSE(app.runtime().axesEnabled());
  }
}

TEST(Application, F5TogglesOutputWithoutChangingPipelineOrWireframe)
{
  for (const auto device : {Jade::Renderer::RayDevice::CPU, Jade::Renderer::RayDevice::GPU}) {
    auto config = HiddenInputApplication();
    config.debug.shortcuts = true;
    config.renderer.rayTracing.enabled = false;
    config.renderer.wireframe = true;
    config.renderer.rayTracing.device = device;
    config.renderer.rayTracing.maxBounces = 7;
    config.renderer.rayTracing.seed = 42;
    config.renderer.rayTracing.minSamplesPerFrame = 3;
    config.renderer.rayTracing.maxSamples = 1;
    config.renderer.rayTracing.resolutionScale = 0.125f;
    Jade::Application app(config);
    MakeTestScene(app);
    auto& renderer = app.runtime().renderer();
    auto expected = renderer.rayTracingSettings();
    KeyboardAdapter input;
    input.Send(GLFW_KEY_F5, GLFW_PRESS);
    input.Send(GLFW_KEY_F5, GLFW_REPEAT);
    app.Run({1});
    expected.output = Jade::Renderer::RayOutput::Normal;
    expected.enabled = false;
    EXPECT_EQ(renderer.rayTracingSettings(), expected);
    EXPECT_EQ(renderer.pipeline(), Jade::Renderer::RenderMode::None);
    EXPECT_FALSE(renderer.progress().samplesPerPixel.has_value());
    EXPECT_TRUE(renderer.wireframeEnabled());
    input.Send(GLFW_KEY_I, GLFW_PRESS);
    input.Send(GLFW_KEY_I, GLFW_RELEASE);
    app.Run({1});
    expected.enabled = false;
    EXPECT_EQ(renderer.rayTracingSettings(), expected);
    EXPECT_FALSE(renderer.progress().samplesPerPixel.has_value());
    input.Send(GLFW_KEY_F5, GLFW_RELEASE);
    input.Send(GLFW_KEY_F5, GLFW_PRESS, GLFW_MOD_SHIFT);
    app.Run({1});
    EXPECT_EQ(renderer.rayTracingSettings(), expected);
    const auto consumed = app.runtime().scene()->on<Jade::KeyPressed<"F5">>([] { return Jade::EventFlow::Stop; });
    input.Send(GLFW_KEY_F5, GLFW_RELEASE);
    input.Send(GLFW_KEY_F5, GLFW_PRESS);
    app.Run({1});
    EXPECT_EQ(renderer.rayTracingSettings(), expected);
    consumed.disconnect();
    input.Send(GLFW_KEY_F5, GLFW_RELEASE);
    input.Send(GLFW_KEY_F5, GLFW_PRESS);
    app.Run({1});
    expected.output = Jade::Renderer::RayOutput::Color;
    expected.enabled = false;
    EXPECT_EQ(renderer.rayTracingSettings(), expected);
    EXPECT_FALSE(renderer.progress().samplesPerPixel.has_value());

    // The settings field and the explicit runtime switch share one enabled state.
    renderer.setRayTracingEnabled(true);
    EXPECT_TRUE(renderer.rayTracingSettings().enabled);
    input.Send(GLFW_KEY_F5, GLFW_RELEASE);
    input.Send(GLFW_KEY_F5, GLFW_PRESS);
    app.Run({1});
    EXPECT_TRUE(renderer.rayTracingEnabled());
    EXPECT_EQ(renderer.rayTracingSettings().output, Jade::Renderer::RayOutput::Normal);
    EXPECT_EQ(renderer.progress().samplesPerPixel, 1u);
    expected.enabled = false;
    renderer.setRayTracingSettings(expected);
    EXPECT_FALSE(renderer.rayTracingEnabled());
    app.Run({1});
    EXPECT_FALSE(renderer.progress().samplesPerPixel.has_value());
  }
  Jade::Application app(HiddenInputApplication());
  MakeTestScene(app);
  const auto settings = app.runtime().renderer().rayTracingSettings();
  KeyboardAdapter input;
  input.Send(GLFW_KEY_F5, GLFW_PRESS);
  app.Run({1});
  EXPECT_EQ(app.runtime().renderer().rayTracingSettings(), settings);
}
