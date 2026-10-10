#include <Jade/Renderer/RayTracing/RayTracing.hpp>
#include <Jade/Jade.hpp>

#include "internal/Core/SceneTransforms.hpp"

#include <gtest/gtest.h>
#include <GLFW/glfw3.h>

#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace {

struct Value {
  int value = 0;
};
static_assert(std::ranges::forward_range<Jade::SceneView<Value>>);
struct OnDestroy {
  std::function<void()> callback;
  explicit OnDestroy(std::function<void()> fn) : callback(std::move(fn)) {}
  ~OnDestroy() { callback(); }
};

Jade::ApplicationConfig Hidden()
{
  Jade::ApplicationConfig config;
  config.window.visible = false;
  config.window.width = 160;
  config.window.height = 120;
  config.frameLimits = 120;
  config.logPerformance = false;
  return config;
}

void SendKey(int code, int action)
{
  const auto window = glfwGetCurrentContext();
  const auto focus = glfwSetWindowFocusCallback(window, nullptr);
  glfwSetWindowFocusCallback(window, focus);
  focus(window, GLFW_TRUE);
  const auto key = glfwSetKeyCallback(window, nullptr);
  glfwSetKeyCallback(window, key);
  key(window, code, 0, action, 0);
}

Jade::Vec3 Point(const Jade::Matrix4 &matrix, Jade::Vec3 point, float w = 1)
{
  return {
      matrix.at(0, 0) * point.x + matrix.at(0, 1) * point.y + matrix.at(0, 2) * point.z + matrix.at(0, 3) * w,
      matrix.at(1, 0) * point.x + matrix.at(1, 1) * point.y + matrix.at(1, 2) * point.z + matrix.at(1, 3) * w,
      matrix.at(2, 0) * point.x + matrix.at(2, 1) * point.y + matrix.at(2, 2) * point.z +
          matrix.at(2, 3) * w};
}

} // namespace

TEST(Scene, StartsEmptyAndViewsDoNotKeepTheOwnerAlive)
{
  Jade::Entity entity;
  std::optional<Jade::SceneView<Value>> snapshot;
  {
    Jade::Scene scene;
    EXPECT_FALSE(scene.camera());
    const auto empty = scene.view<Value>();
    EXPECT_EQ(empty.begin(), empty.end());
    entity = scene.create();
    EXPECT_FALSE(entity.has<Jade::Transform>());
    entity.add<Value>(7);
    snapshot.emplace(scene.view<Value>());
    ASSERT_NE(snapshot->begin(), snapshot->end());
    EXPECT_EQ((*snapshot->begin()).get<Value>().value, 7);
  }
  EXPECT_FALSE(entity.valid());
  EXPECT_EQ(snapshot->begin(), snapshot->end());
}

TEST(Hierarchy, ParentingValidatesBeforeMutationAndPreservesLocalTrs)
{
  Jade::Scene scene, foreign;
  const auto root = scene.create(), child = scene.create(), leaf = scene.create();
  auto &local = child.add<Jade::Transform>();
  local.position = {1, 2, 3};
  local.rotation = Jade::Quaternion::angleAxis({0, 1, 0}, 30);
  local.scale = {2, 3, 4};
  const auto original = local;
  scene.setParent(child, root);
  scene.setParent(leaf, child);
  EXPECT_EQ(scene.parent(child), root);
  EXPECT_EQ(scene.parent(leaf), child);
  EXPECT_THROW(scene.setParent(root, leaf), std::invalid_argument);
  EXPECT_THROW(scene.setParent(root, root), std::invalid_argument);
  EXPECT_THROW(scene.setParent(child, foreign.create()), std::invalid_argument);
  EXPECT_FALSE(scene.parent(root));
  EXPECT_EQ(scene.parent(child), root);
  const auto dead = scene.create();
  scene.destroy(dead);
  EXPECT_THROW(scene.setParent(child, dead), std::invalid_argument);
  scene.setParent(child);
  EXPECT_FALSE(scene.parent(child));
  EXPECT_EQ(scene.parent(leaf), child);
  EXPECT_EQ(local.position, original.position);
  EXPECT_EQ(local.rotation, original.rotation);
  EXPECT_EQ(local.scale, original.scale);
  EXPECT_TRUE(scene.children(root).empty());
}

TEST(Hierarchy, ChildrenSnapshotsRetainAttachmentOrder)
{
  Jade::Scene scene;
  const auto root = scene.create(), a = scene.create(), b = scene.create();
  scene.setParent(a, root);
  scene.setParent(b, root);
  const auto snapshot = scene.children(root);
  scene.setParent(a);
  scene.setParent(a, root);
  scene.setParent(b, root);
  EXPECT_EQ(snapshot, (std::vector<Jade::Entity>{a, b}));
  EXPECT_EQ(scene.children(root), (std::vector<Jade::Entity>{b, a}));
  scene.destroy(b);
  EXPECT_EQ(scene.children(root), (std::vector<Jade::Entity>{a}));
  EXPECT_FALSE(snapshot[1]);
}

TEST(Hierarchy, FullMatricesRetainShearAndDirectEditsAreVisible)
{
  Jade::Scene scene;
  const auto root = scene.create(), child = scene.create(), leaf = scene.create();
  auto &a = root.add<Jade::Transform>();
  a.position = {2, 3, 4};
  a.scale = {2, 3, 4};
  a.rotation = Jade::Quaternion::angleAxis({0, 0, 1}, 30);
  auto &b = child.add<Jade::Transform>();
  b.position = {1, 0, 0};
  b.rotation = Jade::Quaternion::angleAxis({0, 1, 0}, 45);
  auto &c = leaf.add<Jade::Transform>();
  c.position = {0, 0, 2};
  scene.setParent(child, root);
  scene.setParent(leaf, child);
  const auto expected =
      Jade::Detail::ModelMatrix(a) * Jade::Detail::ModelMatrix(b) * Jade::Detail::ModelMatrix(c);
  const auto actual = scene.worldMatrix(leaf);
  for (std::size_t index = 0; index < 16; ++index)
    EXPECT_NEAR(actual.values[index], expected.values[index], 1e-5);
  Jade::Detail::SceneTransforms transforms(scene);
  const auto pose = transforms.Resolve(child);
  const auto tangent = Point(pose.model, {1, 0, 0}, 0);
  const auto normal = Point(pose.normal, {0, 0, 1}, 0);
  EXPECT_NEAR(tangent.dot(normal), 0, 1e-5);
  const auto before = Point(actual, {});
  a.position.x += 5;
  EXPECT_NEAR(Point(scene.worldMatrix(leaf), {}).x, before.x + 5, 1e-5);
  b.scale.y = 0;
  EXPECT_THROW(scene.worldMatrix(leaf), std::invalid_argument);
}

TEST(Hierarchy, GroupsWithoutTransformAreIdentityAndRemovalKeepsRelationships)
{
  Jade::Scene scene;
  const auto group = scene.create(), child = scene.create();
  child.add<Jade::Transform>().position = {1, 2, 3};
  scene.setParent(child, group);
  EXPECT_EQ(Point(scene.worldMatrix(child), {}), (Jade::Vec3{1, 2, 3}));
  group.add<Jade::Transform>().position = {5, 0, 0};
  EXPECT_EQ(Point(scene.worldMatrix(child), {}), (Jade::Vec3{6, 2, 3}));
  group.remove<Jade::Transform>();
  EXPECT_EQ(scene.parent(child), group);
  EXPECT_EQ(Point(scene.worldMatrix(child), {}), (Jade::Vec3{1, 2, 3}));
}

TEST(Hierarchy, SubtreeInvalidationPrecedesAllDestructorsAndAllowsReentry)
{
  Jade::Scene scene;
  const auto outside = scene.create(), root = scene.create(), child = scene.create(), leaf = scene.create();
  scene.setParent(root, outside);
  scene.setParent(child, root);
  scene.setParent(leaf, child);
  std::vector<int> destroyed;
  Jade::Entity replacement;
  const auto checkDead = [&] {
    EXPECT_FALSE(root);
    EXPECT_FALSE(child);
    EXPECT_FALSE(leaf);
  };
  leaf.add<OnDestroy>([&] {
    checkDead();
    destroyed.push_back(3);
    replacement = scene.create();
    replacement.add<Value>(99);
    EXPECT_THROW(scene.setParent(replacement, root), std::invalid_argument);
    EXPECT_THROW(child.add<Value>(), std::logic_error);
  });
  child.add<OnDestroy>([&] {
    checkDead();
    destroyed.push_back(2);
  });
  root.add<OnDestroy>([&] {
    checkDead();
    destroyed.push_back(1);
  });
  scene.destroy(root);
  EXPECT_EQ(destroyed, (std::vector<int>{3, 2, 1}));
  EXPECT_TRUE(outside);
  EXPECT_TRUE(scene.children(outside).empty());
  EXPECT_TRUE(replacement);
  EXPECT_EQ(replacement.get<Value>().value, 99);
  EXPECT_NE(replacement, root);
  EXPECT_NE(replacement, child);
  EXPECT_NE(replacement, leaf);
}

TEST(Hierarchy, DeepTreesResolveAndDeleteWithoutRecursion)
{
  Jade::Scene scene;
  auto root = scene.create(), tail = root;
  root.add<Jade::Transform>().position.x = 1;
  for (int index = 1; index < 4096; ++index) {
    const auto next = scene.create();
    next.add<Jade::Transform>().position.x = 1;
    scene.setParent(next, tail);
    tail = next;
  }
  EXPECT_FLOAT_EQ(scene.worldMatrix(tail).at(0, 3), 4096);
  scene.destroy(root);
  EXPECT_FALSE(tail);
  const auto view = scene.view<Jade::Transform>();
  EXPECT_EQ(view.begin(), view.end());
}

TEST(Hierarchy, CameraUsesInheritedPositionAndRotationWhileIgnoringItsScale)
{
  Jade::Scene scene;
  const auto parent = scene.create(), camera = scene.create();
  auto &a = parent.add<Jade::Transform>();
  a.position = {2, 3, 4};
  a.scale = {2, 3, 4};
  a.rotation = Jade::Quaternion::angleAxis({0, 1, 0}, 30);
  auto &b = camera.add<Jade::Transform>();
  b.position = {0, 0, 5};
  b.rotation = Jade::Quaternion::angleAxis({0, 0, 1}, 20);
  b.scale = {0, std::numeric_limits<float>::quiet_NaN(), -1};
  camera.add<Jade::Camera>();
  scene.setCamera(camera);
  scene.setParent(camera, parent);
  Jade::Detail::SceneTransforms transforms(scene);
  const auto eye = Point(scene.worldMatrix(parent), b.position);
  const auto view = transforms.CameraView(camera);
  const auto origin = Point(view, eye);
  EXPECT_NEAR(origin.x, 0, 1e-5);
  EXPECT_NEAR(origin.y, 0, 1e-5);
  EXPECT_NEAR(origin.z, 0, 1e-5);
  const auto forward = (a.rotation * b.rotation).rotate({0, 0, -1});
  const auto ahead = Point(view, eye + forward * 2);
  EXPECT_NEAR(ahead.x, 0, 1e-5);
  EXPECT_NEAR(ahead.y, 0, 1e-5);
  EXPECT_NEAR(ahead.z, -2, 1e-5);
}

TEST(SceneSwitching, EmptyApplicationAndExplicitSelectionWorkWithoutACamera)
{
  Jade::Application app(Hidden());
  EXPECT_FALSE(app.scene());
  EXPECT_NO_THROW(app.Run({1}));
  auto scene = std::make_shared<Jade::Scene>();
  app.setScene(scene);
  EXPECT_EQ(app.scene(), scene);
  EXPECT_FALSE(scene->camera());
  EXPECT_NO_THROW(app.Run({1}));
  app.setScene(nullptr);
  EXPECT_FALSE(app.scene());
  EXPECT_NO_THROW(app.Run({1}));
}

TEST(SceneSwitching, DeferredSwitchRetainsOneScenePerPassAndResetsDt)
{
  Jade::Application app(Hidden());
  auto a = std::make_shared<Jade::Scene>(), b = std::make_shared<Jade::Scene>();
  const auto entity = a->create();
  entity.add<Value>(7);
  std::vector<std::string> order;
  std::vector<Jade::FrameUpdate> updates;
  a->on<Jade::SceneActivated>([&] { order.push_back("a-enter"); });
  a->on<Jade::SceneDeactivated>([&] { order.push_back("a-exit"); });
  b->on<Jade::SceneActivated>([&] { order.push_back("b-enter"); });
  bool switchOnce = true;
  const auto connection = a->on<Jade::FrameUpdate>([&](const auto &) {
    order.push_back("a-update");
    if (switchOnce) {
      switchOnce = false;
      app.setScene(b);
      EXPECT_EQ(app.scene(), a);
    }
  });
  a->on<Jade::FrameUpdate>([&] { order.push_back("a-late"); });
  b->on<Jade::FrameUpdate>([&](const auto &update) {
    order.push_back("b-update");
    updates.push_back(update);
  });
  app.setScene(a);
  app.Run({3});
  EXPECT_EQ(order, (std::vector<std::string>{"a-enter", "a-update", "a-late", "a-exit", "b-enter", "b-update",
                                             "b-update"}));
  ASSERT_EQ(updates.size(), 2u);
  EXPECT_EQ(updates[0].frame, 2u);
  EXPECT_EQ(updates[1].frame, 3u);
  EXPECT_FLOAT_EQ(updates[0].dt, 0);
  EXPECT_GT(updates[1].dt, 0);
  EXPECT_GT(updates[1].elapsed, updates[0].elapsed);
  EXPECT_TRUE(connection.connected());
  EXPECT_EQ(entity.get<Value>().value, 7);
  order.clear();
  app.setScene(a);
  app.Run({1});
  EXPECT_EQ(order, (std::vector<std::string>{"a-enter", "a-update", "a-late"}));
}

TEST(SceneSwitching, LastRequestWinsAndSelectingTheSameSceneDoesNotNotify)
{
  Jade::Application app(Hidden());
  auto a = std::make_shared<Jade::Scene>(), b = std::make_shared<Jade::Scene>(),
       c = std::make_shared<Jade::Scene>();
  int exits = 0, bEnters = 0, cEnters = 0;
  a->on<Jade::SceneDeactivated>([&] { ++exits; });
  b->on<Jade::SceneActivated>([&] { ++bEnters; });
  c->on<Jade::SceneActivated>([&] { ++cEnters; });
  app.on<Jade::FrameUpdate>([&](const auto &event) {
    if (event.frame == 1) {
      app.setScene(b);
      app.setScene(c);
    }
  });
  app.setScene(a);
  app.Run({2});
  EXPECT_EQ(app.scene(), c);
  EXPECT_EQ(exits, 1);
  EXPECT_EQ(bEnters, 0);
  EXPECT_EQ(cEnters, 1);
  app.setScene(c);
  EXPECT_EQ(cEnters, 1);
  app.setScene(nullptr);
  EXPECT_FALSE(app.scene());
}

TEST(SceneSwitching, LifecycleReentryIsDeferredAndConsumptionDoesNotCancelSwitching)
{
  Jade::Application app(Hidden());
  auto a = std::make_shared<Jade::Scene>(), b = std::make_shared<Jade::Scene>();
  int enterA = 0, enterB = 0, skipped = 0;
  a->on<Jade::SceneActivated>([&] {
    ++enterA;
    app.setScene(b);
    return Jade::EventFlow::Stop;
  });
  a->on<Jade::SceneActivated>([&] { ++skipped; });
  a->on<Jade::SceneDeactivated>([] { return Jade::EventFlow::Stop; });
  a->on<Jade::SceneDeactivated>([&] { ++skipped; });
  b->on<Jade::SceneActivated>([&] { ++enterB; });
  app.setScene(a);
  EXPECT_EQ(app.scene(), a);
  EXPECT_EQ(enterA, 1);
  app.Run({1});
  EXPECT_EQ(app.scene(), b);
  EXPECT_EQ(enterB, 1);
  EXPECT_EQ(skipped, 0);
}

TEST(SceneSwitching, LifecycleExceptionsLeaveSelectionAtTheCompletedPhase)
{
  Jade::Application app(Hidden());
  auto a = std::make_shared<Jade::Scene>(), b = std::make_shared<Jade::Scene>();
  bool failExit = true, failEnter = true;
  a->on<Jade::SceneDeactivated>([&] {
    if (failExit)
      throw std::runtime_error("exit failed");
  });
  b->on<Jade::SceneActivated>([&] {
    if (failEnter)
      throw std::runtime_error("enter failed");
  });
  app.setScene(a);
  EXPECT_THROW(app.setScene(b), std::runtime_error);
  EXPECT_EQ(app.scene(), a);
  failExit = false;
  EXPECT_THROW(app.setScene(b), std::runtime_error);
  EXPECT_EQ(app.scene(), b);
  EXPECT_NO_THROW(app.Run({1}));
  failEnter = false;
  app.setScene(nullptr);
  EXPECT_NO_THROW(app.setScene(b));
}

TEST(SceneSwitching, CallbackFailureDiscardsQueuedSwitchAndRunCanRecover)
{
  Jade::Application app(Hidden());
  auto a = std::make_shared<Jade::Scene>(), b = std::make_shared<Jade::Scene>();
  bool fail = true;
  a->on<Jade::FrameUpdate>([&] {
    if (fail) {
      app.setScene(b);
      throw std::runtime_error("update failed");
    }
  });
  app.setScene(a);
  EXPECT_THROW(app.Run({1}), std::runtime_error);
  EXPECT_EQ(app.scene(), a);
  fail = false;
  EXPECT_NO_THROW(app.Run({1}));
  EXPECT_EQ(app.scene(), a);
}

TEST(SceneSwitching, SceneCanOnlyBeActiveInOneApplicationAndHandlesExpireAtOwnerDestruction)
{
  auto scene = std::make_shared<Jade::Scene>();
  const auto entity = scene->create();
  entity.add<Value>();
  const auto connection = scene->on<Jade::FrameUpdate>([] {});
  {
    Jade::Application first(Hidden()), second(Hidden());
    first.setScene(scene);
    EXPECT_THROW(second.setScene(scene), std::invalid_argument);
    EXPECT_FALSE(second.scene());
    EXPECT_EQ(first.scene(), scene);
    first.setScene(nullptr);
    second.setScene(scene);
  }
  EXPECT_TRUE(entity);
  EXPECT_TRUE(connection.connected());
  // Application destruction released the activation reservation.
  {
    Jade::Application app(Hidden());
    EXPECT_NO_THROW(app.setScene(scene));
  }
  scene.reset();
  EXPECT_FALSE(entity);
  EXPECT_FALSE(connection.connected());
}

TEST(SceneSwitching, LifecycleReservesTargetAndFailedActivationDiscardsReentrantSelection)
{
  Jade::Application app(Hidden()), other(Hidden());
  auto a = std::make_shared<Jade::Scene>();
  auto b = std::make_shared<Jade::Scene>();
  auto c = std::make_shared<Jade::Scene>();
  bool requested = false;
  bool failActivation = true;
  int cActivated = 0;
  a->on<Jade::SceneDeactivated>([&] {
    EXPECT_EQ(app.scene(), a);
    EXPECT_THROW(other.setScene(a), std::invalid_argument);
    EXPECT_THROW(other.setScene(b), std::invalid_argument);
  });
  b->on<Jade::SceneActivated>([&] {
    EXPECT_EQ(app.scene(), b);
    // The old owner is released only after committing the new selection.
    EXPECT_NO_THROW(other.setScene(a));
    EXPECT_THROW(app.Run({1}), std::logic_error);
    if (failActivation) {
      app.setScene(c);
      throw std::runtime_error("activation failed");
    }
  });
  c->on<Jade::SceneActivated>([&] { ++cActivated; });
  app.on<Jade::FrameUpdate>([&] {
    if (!requested) {
      requested = true;
      app.setScene(b);
    }
  });
  app.setScene(a);
  EXPECT_THROW(app.Run({2}), std::runtime_error);
  EXPECT_EQ(app.scene(), b);
  EXPECT_EQ(other.scene(), a);
  failActivation = false;
  EXPECT_NO_THROW(app.Run({1}));
  EXPECT_EQ(app.scene(), b);
  EXPECT_EQ(cActivated, 0);
}

TEST(SceneSwitching, GlobalAndSceneEventsPreservePropagationAndPhysicalHeldState)
{
  Jade::Application app(Hidden());
  auto a = std::make_shared<Jade::Scene>(), b = std::make_shared<Jade::Scene>();
  int pressedA = 0, heldA = 0, pressedB = 0, heldB = 0, swallowed = 0;
  std::vector<int> order;
  app.on<Jade::KeyPressed<"w">>([&] { order.push_back(1); });
  a->on<Jade::KeyPressed<"w">>([&] {
    ++pressedA;
    order.push_back(2);
  });
  a->on<Jade::KeyHeld<"w">>([&] { ++heldA; });
  b->on<Jade::KeyPressed<"w">>([&] { ++pressedB; });
  b->on<Jade::KeyHeld<"w">>([&](const auto &event) {
    ++heldB;
    EXPECT_FLOAT_EQ(event.dt, 0);
  });
  app.on<Jade::KeyPressed<"1">>([&] {
    app.setScene(b);
    return Jade::EventFlow::Stop;
  });
  a->on<Jade::KeyPressed<"1">>([&] { ++swallowed; });
  app.setScene(a);
  SendKey(GLFW_KEY_W, GLFW_PRESS);
  SendKey(GLFW_KEY_1, GLFW_PRESS);
  app.Run({2});
  EXPECT_EQ(order, (std::vector<int>{1, 2}));
  EXPECT_EQ(pressedA, 1);
  EXPECT_EQ(heldA, 1);
  EXPECT_EQ(pressedB, 0);
  EXPECT_EQ(heldB, 1);
  EXPECT_EQ(swallowed, 0);
  EXPECT_EQ(app.scene(), b);
}

TEST(SceneSwitching, SceneConsumptionPreventsDefaultEscapeAndGlobalUpdateStillRuns)
{
  Jade::Application app(Hidden());
  auto scene = std::make_shared<Jade::Scene>();
  int globalUpdates = 0, sceneUpdates = 0, laterUpdates = 0;
  app.on<Jade::FrameUpdate>([&] { ++globalUpdates; });
  scene->on<Jade::FrameUpdate>([&] {
    ++sceneUpdates;
    return Jade::EventFlow::Stop;
  });
  scene->on<Jade::FrameUpdate>([&] { ++laterUpdates; });
  scene->on<Jade::KeyPressed<"Escape">>([] { return Jade::EventFlow::Stop; });
  app.setScene(scene);
  SendKey(GLFW_KEY_ESCAPE, GLFW_PRESS);
  EXPECT_NO_THROW(app.Run({2}));
  EXPECT_EQ(globalUpdates, 2);
  EXPECT_EQ(sceneUpdates, 2);
  EXPECT_EQ(laterUpdates, 0);
}
