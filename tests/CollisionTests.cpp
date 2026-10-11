#include <Jade/Jade.hpp>
#include "internal/Physics/CollisionWorld.hpp"
#include "internal/Renderer/Debug/CollisionDebug.hpp"
#include "internal/Renderer/Debug/DebugDraw.hpp"
#include "internal/Renderer/Presentation/OpenGLDisplay.hpp"
#include "internal/Platform/Window.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <thread>
#include <type_traits>

namespace {
using namespace Jade;

template<class Shape> Entity add(Scene& scene, Shape shape, Vec3 position = {})
{
  const auto entity = scene.create();
  entity.add<Transform>().position = position;
  entity.add<Collider>(shape);
  return entity;
}

void near(Vec3 actual, Vec3 expected, float tolerance = 1e-5f)
{
  EXPECT_NEAR(actual.x, expected.x, tolerance);
  EXPECT_NEAR(actual.y, expected.y, tolerance);
  EXPECT_NEAR(actual.z, expected.z, tolerance);
}

void symmetric(const Scene& scene, Entity a, Entity b)
{
  const auto forward = Physics::contact(scene, a, b), reverse = Physics::contact(scene, b, a);
  ASSERT_TRUE(forward);
  ASSERT_TRUE(reverse);
  EXPECT_EQ(forward->a, a);
  EXPECT_EQ(forward->b, b);
  EXPECT_EQ(reverse->a, b);
  EXPECT_EQ(reverse->b, a);
  near(forward->point, reverse->point);
  near(forward->normal, -reverse->normal);
  EXPECT_FLOAT_EQ(forward->penetration, reverse->penetration);
  EXPECT_NEAR(dot(forward->normal, forward->normal), 1, 1e-5);
}

EngineConfig hidden()
{
  EngineConfig config;
  config.window.visible = false;
  config.window.width = config.window.height = 64;
  config.debug.axes = false;
  config.logPerformance = false;
  config.frameLimits = 240;
  config.jobs.workerCount = 1;
  config.renderer.rayTracing.maxSamples = 1;
  return config;
}

Entity camera(Scene& scene)
{
  const auto entity = scene.create();
  entity.add<Transform>().position = {0, 0, 5};
  entity.add<Camera>();
  scene.setCamera(entity);
  return entity;
}

} // namespace

TEST(Collider, ShapeValuesCopyAndAssignWithoutChangingEntityAPI)
{
  static_assert(std::is_trivially_copyable_v<Collider>);
  static_assert(std::is_nothrow_default_constructible_v<Collider>);
  Scene scene;
  const auto a = add(scene, Box{{1, 2, 1}}), b = add(scene, Sphere{1}, {1.5f, 0, 0});
  ASSERT_TRUE(Physics::contact(scene, a, b));
  const auto copy = a.get<Collider>();
  a.get<Collider>() = Collider{Sphere{0.25f}};
  EXPECT_FALSE(Physics::contact(scene, a, b));
  a.get<Collider>() = copy;
  EXPECT_TRUE(Physics::contact(scene, a, b));
}

TEST(Collider, EmptyShapesSkipQueriesAndDebugBeforeTransformValidation)
{
  constexpr Collider empty;
  static_assert(empty.empty());
  Scene scene;
  const auto a = scene.create<Archetype<Transform, Collider>>();
  const auto b = scene.create<Archetype<Transform, Collider>>();
  a.get<Transform>().scale = {};
  b.get<Transform>().rotation = {0, 0, 0, 0};
  EXPECT_FALSE(Physics::contact(scene, a, b));
  EXPECT_TRUE(Physics::detect(scene).empty());
  EXPECT_TRUE(Physics::manifolds(scene).empty());
  auto debug = Detail::captureCollisionDebug(scene, true, true, true);
  EXPECT_TRUE(debug.shapes.empty());
  EXPECT_TRUE(debug.aabbs.empty());
  EXPECT_TRUE(debug.contacts.empty());
  a.get<Transform>() = {}; b.get<Transform>() = {};
  a.get<Collider>() = Collider{Box{}};
  b.get<Collider>() = Collider{Sphere{0.5f}};
  EXPECT_TRUE(Physics::contact(scene, a, b));
  EXPECT_EQ(Physics::detect(scene).size(), 1u);
  const auto shape = a.get<Collider>();
  a.get<Collider>() = {};
  const auto emptyCopy = a.get<Collider>();
  EXPECT_TRUE(emptyCopy.empty());
  EXPECT_FALSE(shape.empty());
  EXPECT_TRUE(Physics::detect(scene).empty());
  a.get<Collider>() = shape;
  EXPECT_TRUE(Physics::contact(scene, a, b));
  b.get<Collider>() = emptyCopy;
  EXPECT_FALSE(Physics::contact(scene, a, b));
}

TEST(Collision, SpheresSeparateTouchOverlapContainAndShareCenters)
{
  Scene scene;
  const auto a = add(scene, Sphere{1}), b = add(scene, Sphere{1}, {3, 0, 0});
  EXPECT_FALSE(Physics::contact(scene, a, b));
  b.get<Transform>().position.x = 2;
  auto hit = Physics::contact(scene, a, b);
  ASSERT_TRUE(hit);
  EXPECT_FLOAT_EQ(hit->penetration, 0);
  near(hit->point, {1, 0, 0});
  b.get<Transform>().position.x = 1.5f;
  hit = Physics::contact(scene, a, b);
  ASSERT_TRUE(hit);
  EXPECT_FLOAT_EQ(hit->penetration, 0.5f);
  near(hit->point, {0.75f, 0, 0});
  near(hit->normal, {1, 0, 0});
  symmetric(scene, a, b);
  b.get<Collider>() = Collider{Sphere{3}};
  b.get<Transform>().position = {};
  hit = Physics::contact(scene, a, b);
  ASSERT_TRUE(hit);
  EXPECT_FLOAT_EQ(hit->penetration, 4);
  near(hit->normal, {1, 0, 0});
  symmetric(scene, a, b);
}

TEST(Collision, SphereBoxRejectsAabbFalsePositivesAndHandlesCorners)
{
  Scene scene;
  const auto sphere = add(scene, Sphere{0.25f}, {1.2f, 1.2f, 0}), box = add(scene, Box{{1, 1, 1}});
  const auto proxies = Detail::captureColliders(scene);
  ASSERT_TRUE(proxies[0].bounds.overlaps(proxies[1].bounds, 0));
  EXPECT_FALSE(Physics::contact(scene, sphere, box));
  sphere.get<Collider>() = Collider{Sphere{0.3f}};
  auto hit = Physics::contact(scene, sphere, box);
  ASSERT_TRUE(hit);
  const float diagonal = std::sqrt(0.5f);
  near(hit->normal, {-diagonal, -diagonal, 0});
  EXPECT_NEAR(hit->penetration, 0.3f - std::sqrt(0.08f), 1e-6);
  symmetric(scene, sphere, box);
  sphere.get<Transform>().position = {1.3f, 0, 0};
  hit = Physics::contact(scene, sphere, box);
  ASSERT_TRUE(hit);
  EXPECT_NEAR(hit->penetration, 0, 1e-6);
  near(hit->point, {1, 0, 0});
}

TEST(Collision, SphereInsideRotatedBoxUsesNearestExitAndCorrectSeparationNormal)
{
  Scene scene;
  const auto sphere = add(scene, Sphere{0.25f}), box = add(scene, Box{{1, 2, 3}});
  auto& transform = box.get<Transform>();
  Motion::yaw(transform, 45);
  const auto right = Motion::getRight(transform);
  sphere.get<Transform>().position = right * 0.75f;
  const auto hit = Physics::contact(scene, sphere, box);
  ASSERT_TRUE(hit);
  near(hit->normal, -right);
  EXPECT_NEAR(hit->penetration, 0.5f, 1e-6);
  near(hit->point, right * 0.75f);
  symmetric(scene, sphere, box);
  transform.position = hit->normal * (hit->penetration + 1e-3f);
  EXPECT_FALSE(Physics::contact(scene, sphere, box));
}

TEST(Collision, SphereBoxNormalSurvivesWorldCoordinateCancellation)
{
  Scene scene;
  const float origin = std::ldexp(1.0f, 125), offset = std::ldexp(1.0f, 102);
  const float step = std::ldexp(1.0f, -23);
  const auto sphere = add(scene, Sphere{1e18f}, {origin + offset, 0, 0});
  const auto box = add(scene, Box{{offset * (1 - step), 1, 1}}, {origin, 0, 0});
  box.get<Transform>().scale.x = 1 + step;
  // The exact closest displacement is smaller than the double ULP at origin.
  // Computing an absolute closest point and subtracting it would erase it.
  const auto hit = Physics::contact(scene, sphere, box);
  ASSERT_TRUE(hit);
  near(hit->normal, {-1, 0, 0});
  EXPECT_GT(hit->penetration, 0);
  EXPECT_TRUE(std::isfinite(hit->penetration));
  EXPECT_TRUE(std::isfinite(hit->point.x));
  symmetric(scene, sphere, box);
}

TEST(Collision, BoxesProduceFaceContactsAndCorrectContainmentDepth)
{
  Scene scene;
  const auto a = add(scene, Box{{1, 1, 1}}), b = add(scene, Box{{1, 1, 1}}, {1.5f, 0, 0});
  auto hit = Physics::contact(scene, a, b);
  ASSERT_TRUE(hit);
  near(hit->normal, {1, 0, 0});
  near(hit->point, {0.75f, 0, 0});
  EXPECT_NEAR(hit->penetration, 0.5f, 1e-6);
  symmetric(scene, a, b);
  b.get<Transform>().position.x = 2;
  hit = Physics::contact(scene, a, b);
  ASSERT_TRUE(hit);
  EXPECT_FLOAT_EQ(hit->penetration, 0);
  a.get<Collider>() = Collider{Box{{2, 2, 2}}};
  b.get<Collider>() = Collider{Box{{0.5f, 0.5f, 0.5f}}};
  b.get<Transform>().position.x = 0.5f;
  hit = Physics::contact(scene, a, b);
  ASSERT_TRUE(hit);
  EXPECT_NEAR(hit->penetration, 2, 1e-6);
  near(hit->point, {1, 0, 0});
  b.get<Transform>().position.x += hit->penetration + 1e-3f;
  EXPECT_FALSE(Physics::contact(scene, a, b));
}

TEST(Collision, SkewBoxesNeedEdgeAxesForSeparationAndEdgeContacts)
{
  Scene scene;
  const auto a = add(scene, Box{{2, 0.1f, 0.1f}}), b = add(scene, Box{{2, 0.1f, 0.1f}});
  auto& pose = b.get<Transform>();
  pose.rotation = Quaternion::angleAxis({1, 0, 0}, 45) * Quaternion::angleAxis({0, 0, 1}, 90) *
                  Quaternion::angleAxis({1, 0, 0}, 45);
  pose.position = {0, -0.23f, 0.23f};
  const auto proxies = Detail::captureColliders(scene);
  ASSERT_TRUE(proxies[0].bounds.overlaps(proxies[1].bounds, 0));
  EXPECT_FALSE(Physics::contact(scene, a, b));
  pose.position = {0, -0.2f, 0.2f};
  auto hit = Physics::contact(scene, a, b);
  ASSERT_TRUE(hit);
  EXPECT_NEAR(hit->penetration, 0, 1e-6);
  pose.position = {0, -0.18f, 0.18f};
  hit = Physics::contact(scene, a, b);
  ASSERT_TRUE(hit);
  const float diagonal = std::sqrt(0.5f);
  near(hit->normal, {0, -diagonal, diagonal});
  near(hit->point, {0, -0.09f, 0.09f});
  EXPECT_NEAR(hit->penetration, 0.04f * diagonal, 1e-6);
  symmetric(scene, a, b);
}

TEST(Collision, RandomRotatedBoxesSeparateAlongReportedMinimumTranslation)
{
  Scene scene;
  const auto a = add(scene, Box{}), b = add(scene, Box{});
  std::mt19937 random(573);
  std::uniform_real_distribution<float> angle(-180, 180), size(0.2f, 2), position(-1, 1);
  for (int i = 0; i < 250; ++i) {
    SCOPED_TRACE(i);
    const Vec3 halfA{size(random), size(random), size(random)}, halfB{size(random), size(random), size(random)};
    a.get<Collider>() = Collider{Box{halfA}};
    b.get<Collider>() = Collider{Box{halfB}};
    a.get<Transform>().rotation = Quaternion::angleAxis({1, 2, 3}, angle(random));
    b.get<Transform>().rotation = Quaternion::angleAxis({3, -1, 2}, angle(random));
    // Both boxes contain the origin, independently guaranteeing intersection.
    const float minA = std::min({halfA.x, halfA.y, halfA.z}), minB = std::min({halfB.x, halfB.y, halfB.z});
    a.get<Transform>().position = Vec3{position(random), position(random), position(random)} * (minA * 0.2f);
    b.get<Transform>().position = Vec3{position(random), position(random), position(random)} * (minB * 0.2f);
    const auto hit = Physics::contact(scene, a, b);
    ASSERT_TRUE(hit);
    EXPECT_NEAR(dot(hit->normal, hit->normal), 1, 1e-5);
    EXPECT_TRUE(std::isfinite(hit->point.x) && std::isfinite(hit->point.y) && std::isfinite(hit->point.z));
    const auto before = b.get<Transform>().position;
    b.get<Transform>().position = before + hit->normal * (hit->penetration * 0.5f);
    EXPECT_TRUE(Physics::contact(scene, a, b));
    b.get<Transform>().position = before + hit->normal * (hit->penetration + 1e-3f);
    EXPECT_FALSE(Physics::contact(scene, a, b));
  }
}

TEST(Collision, HierarchyAndNonuniformBoxScaleProduceFreshWorldAabbs)
{
  Scene scene;
  const auto parent = scene.create();
  auto& root = parent.add<Transform>();
  root.position = {3, 4, 5};
  root.scale = {2, 2, 2};
  Motion::yaw(root, 45);
  const auto box = add(scene, Box{{1, 2, 1}}, {1, 0, 0});
  box.get<Transform>().scale = {1, 2, 3};
  scene.setParent(box, parent);
  const auto proxies = Detail::captureColliders(scene);
  ASSERT_EQ(proxies.size(), 1u);
  const auto world = scene.worldMatrix(box);
  Detail::CollisionVector minimum{1e30, 1e30, 1e30}, maximum{-1e30, -1e30, -1e30};
  for (int i = 0; i < 8; ++i) {
    const Vec3 local{(i & 1) ? 1.0f : -1.0f, (i & 2) ? 2.0f : -2.0f, (i & 4) ? 1.0f : -1.0f};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const double coordinate = world.at(axis, 3) + world.at(axis, 0) * local.x +
                                world.at(axis, 1) * local.y + world.at(axis, 2) * local.z;
      minimum[axis] = std::min(minimum[axis], coordinate);
      maximum[axis] = std::max(maximum[axis], coordinate);
    }
  }
  near(proxies[0].bounds.minimum.vector(), minimum.vector());
  near(proxies[0].bounds.maximum.vector(), maximum.vector());
  root.position.x += 7;
  const auto moved = Detail::captureColliders(scene);
  EXPECT_NEAR(moved[0].bounds.minimum.x, minimum.x + 7, 1e-5);
}

TEST(Collision, UniformlyScaledSpheresUseFullHierarchyAndIgnoreOrientation)
{
  Scene scene;
  const auto parent = scene.create();
  parent.add<Transform>().scale = {2, 2, 2};
  const auto a = add(scene, Sphere{0.5f}), b = add(scene, Sphere{1}, {2.5f, 0, 0});
  a.get<Transform>().scale = {2, 2, 2};
  Motion::yaw(a.get<Transform>(), 71);
  scene.setParent(a, parent);
  const auto hit = Physics::contact(scene, a, b);
  ASSERT_TRUE(hit);
  EXPECT_NEAR(hit->penetration, 0.5f, 1e-5);
  const auto proxies = Detail::captureColliders(scene);
  near(proxies[0].bounds.minimum.vector(), {-2, -2, -2});
  near(proxies[0].bounds.maximum.vector(), {2, 2, 2});
}

TEST(Collision, InvalidDimensionsScaleShearAndOverflowAreRejected)
{
  Scene scene;
  const auto a = add(scene, Sphere{1}), b = add(scene, Box{});
  for (const float invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
    a.get<Collider>() = Collider{Sphere{invalid}};
    EXPECT_THROW(Physics::detect(scene), std::invalid_argument);
    a.get<Collider>() = Collider{Box{{1, invalid, 1}}};
    EXPECT_THROW(Physics::contact(scene, a, b), std::invalid_argument);
  }
  a.get<Collider>() = Collider{Sphere{1}};
  a.get<Transform>().scale = {1, 2, 1};
  EXPECT_THROW(Physics::detect(scene), std::invalid_argument);
  a.get<Collider>() = Collider{Box{}};
  EXPECT_NO_THROW(Physics::detect(scene));
  const auto child = add(scene, Box{});
  scene.setParent(child, a);
  Motion::yaw(child.get<Transform>(), 30);
  Motion::pitch(child.get<Transform>(), 30);
  EXPECT_THROW(Physics::detect(scene), std::invalid_argument);
  scene.destroy(child);
  a.get<Transform>().scale = {0, 1, 1};
  EXPECT_THROW(Physics::detect(scene), std::invalid_argument);
  a.get<Transform>() = Transform{};
  a.get<Collider>() = Collider{Box{{std::numeric_limits<float>::max(), 1, 1}}};
  a.get<Transform>().scale.x = 2;
  EXPECT_THROW(Physics::detect(scene), std::invalid_argument);
}

TEST(Collision, QueriesHandleMissingComponentsForeignEntitiesAndStablePairOrdering)
{
  Scene scene, other;
  EXPECT_TRUE(Physics::detect(scene).empty());
  const auto a = add(scene, Sphere{1}), b = add(scene, Sphere{1}), c = add(scene, Box{});
  const auto results = Physics::detect(scene);
  ASSERT_EQ(results.size(), 3u);
  EXPECT_EQ(results[0].a, a);
  EXPECT_EQ(results[0].b, b);
  EXPECT_EQ(results[1].a, a);
  EXPECT_EQ(results[1].b, c);
  EXPECT_EQ(results[2].a, b);
  EXPECT_EQ(results[2].b, c);
  EXPECT_FALSE(Physics::contact(scene, a, a));
  const auto foreign = add(other, Sphere{1});
  EXPECT_THROW(Physics::contact(scene, a, foreign), std::invalid_argument);
  a.remove<Transform>();
  EXPECT_FALSE(Physics::contact(scene, a, b));
  EXPECT_EQ(Physics::detect(scene).size(), 1u);
  a.add<Transform>();
  a.remove<Collider>();
  EXPECT_FALSE(Physics::contact(scene, a, b));
  scene.destroy(b);
  EXPECT_THROW(Physics::contact(scene, b, c), std::invalid_argument);
  EXPECT_TRUE(Physics::detect(scene).empty());
}

TEST(CollisionDebug, CapturesRealShapesAndAabbsForEntitiesWithoutGeometry)
{
  Scene scene;
  add(scene, Box{{1, 2, 1}});
  const auto sphere = add(scene, Sphere{1}, {4, 0, 0});
  auto debug = Detail::captureCollisionDebug(scene, true, true);
  EXPECT_EQ(debug.shapes.size(), 24u + 3 * 64 * 2);
  EXPECT_EQ(debug.aabbs.size(), 48u);
  sphere.remove<Transform>();
  debug = Detail::captureCollisionDebug(scene, false, true);
  EXPECT_TRUE(debug.shapes.empty());
  EXPECT_EQ(debug.aabbs.size(), 24u);
  EXPECT_TRUE(Detail::captureCollisionDebug(scene, false, false).aabbs.empty());
}

TEST(CollisionDebug, ShapeAndAabbOverlaysProduceIndependentColorsOverImageAndWireframe)
{
  WindowConfig config;
  config.visible = false;
  config.width = config.height = 64;
  Window window(config);
  Detail::OpenGLDevice device(window, false);
  Detail::OpenGLDisplay display(device);
  Detail::DebugDraw draw(device);
  Scene scene;
  camera(scene);
  add(scene, Sphere{0.75f}, {1, 0, 0});
  Assets assets;
  const auto mesh = scene.create();
  mesh.add<Transform>().position = {-1, 0, 0};
  mesh.add<Geometry>(assets.add(MeshData::cube(1.5f)));
  mesh.add<Material>(Materials::Diffuse({1, 1, 1}));
  const auto snapshot = Renderer::SceneSnapshot::capture(scene, assets);
  const auto shape = Detail::captureCollisionDebug(scene, true, false);
  const auto bounds = Detail::captureCollisionDebug(scene, false, true);
  std::vector<unsigned char> pixels(64 * 64 * 4);
  const Renderer::Image image{{1, 1}, {0, 0, 0, 255}};
  for (const bool wireframe : {false, true}) {
    for (const bool aabb : {false, true}) {
      display.begin({64, 64});
      if (wireframe)
        draw.wireframe(snapshot, {64, 64});
      else
        display.image(image);
      draw.colliders(aabb ? bounds : shape, *snapshot.camera, {64, 64});
      glReadBuffer(GL_BACK);
      glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      int colored = 0, meshEdges = 0;
      for (std::size_t i = 0; i < pixels.size(); i += 4) {
        if (aabb ? (pixels[i] < 10 && pixels[i + 1] > 240 && pixels[i + 2] > 240)
                 : (pixels[i] > 240 && pixels[i + 1] > 110 && pixels[i + 1] < 145 && pixels[i + 2] < 10))
          ++colored;
        if (pixels[i] > 195 && pixels[i] < 215 && pixels[i + 1] > 205 && pixels[i + 1] < 230 &&
            pixels[i + 2] > 220 && pixels[i + 2] < 240)
          ++meshEdges;
      }
      EXPECT_GT(colored, 50);
      if (wireframe)
        EXPECT_GT(meshEdges, 20);
      else
        EXPECT_EQ(meshEdges, 0);
      EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }
  }
}

TEST(CollisionDebug, ApplicationControlsAreOptInAndValidateOnlyWhenDisplayed)
{
  auto config = hidden();
  EXPECT_FALSE(config.debug.physics);
  Application app(config);
  const auto scene = std::make_shared<Scene>();
  camera(*scene);
  add(*scene, Sphere{0});
  app.runtime().setScene(scene);
  EXPECT_NO_THROW(app.Run({1}));
  app.runtime().setPhysicsDebug(true);
  EXPECT_TRUE(app.runtime().physicsDebugEnabled());
  EXPECT_THROW(app.Run({1}), std::invalid_argument);
  app.runtime().setPhysicsDebug(false);
  app.runtime().renderer().setRender(false); app.runtime().renderer().setWireframe(true);
  EXPECT_NO_THROW(app.Run({1}));
}

TEST(CollisionDebug, NearlyParallelBoxesDrawPersistentSolverAnchorsAndImpulses)
{
  Scene scene;
  const auto a = add(scene, Box{}, {-1, 0, 0}), b = add(scene, Box{});
  a.add<RigidBody>(); b.add<RigidBody>();
  Physics::Config config; scene.physics().fields.clear();
  Physics::System simulation(config);
  simulation.step(scene, 1.0f / 120);
  b.get<RigidBody>().angularVelocity = {0.0004f, 0.0006f, 0.0002f};
  b.get<RigidBody>().velocity.z = 0.00012f;
  for (int step = 0; step < 240; ++step) simulation.step(scene, 1.0f / 120);
  const auto colliders = Detail::captureColliders(scene);
  const auto debug = Detail::captureCollisionDebug(scene, false, false, true, &simulation);
  ASSERT_EQ(debug.contacts.size(), 1u);
  const auto& manifold = debug.contacts.front();
  ASSERT_EQ(simulation.contacts().size(), 1u);
  ASSERT_EQ(manifold.points.size(), 4u);
  EXPECT_GT(manifold.normal.x, 0.99f);
  constexpr std::size_t markerVertices = 3 * 16 * 2;
  ASSERT_EQ(debug.points.size(), manifold.points.size() * markerVertices);
  ASSERT_EQ(debug.normals.size(), manifold.points.size() * 6);
  for (std::size_t i = 0; i < manifold.points.size(); ++i) {
    const auto& contact = manifold.points[i];
    const auto& solved = simulation.contacts().front().points[i];
    EXPECT_EQ(contact.pointA, solved.pointA); EXPECT_EQ(contact.pointB, solved.pointB);
    EXPECT_FLOAT_EQ(contact.normalImpulse, solved.normalImpulse);
    EXPECT_EQ(contact.frictionImpulse, solved.frictionImpulse);
    for (std::size_t body = 0; body < 2; ++body) {
      const auto p = body == 0 ? contact.pointA : contact.pointB;
      const auto& box = colliders[body];
      const auto offset = Detail::CollisionVector{p.x, p.y, p.z} - box.center;
      double surfaceDistance = std::numeric_limits<double>::infinity();
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const double coordinate = std::abs(Detail::collisionDot(offset, box.axes[axis]));
        EXPECT_LE(coordinate, box.halfExtents[axis] + 1e-4);
        surfaceDistance = std::min(surfaceDistance, std::abs(coordinate - box.halfExtents[axis]));
      }
      EXPECT_LT(surfaceDistance, 1e-4);
      // A tiny rotation must not replace corner anchors with a vertex that
      // moves macroscopic distances along the middle of the vertical edge.
      EXPECT_NEAR(std::abs(Detail::collisionDot(offset, box.axes[1])), box.halfExtents.y, 1e-4);
    }
    const auto delta = contact.pointB - contact.pointA;
    const auto tangent = delta - manifold.normal * contact.separation;
    EXPECT_LE(dot(tangent, tangent), config.contactDistance * config.contactDistance);
    const auto center = (contact.pointA + contact.pointB) * 0.5f;
    near(debug.normals[i * 6], center);
    near(debug.normals[i * 6 + 1], center + manifold.normal * 0.25f);
    Vec3 markerCenter;
    for (std::size_t vertex = 0; vertex < markerVertices; ++vertex) {
      const auto p = debug.points[i * markerVertices + vertex];
      markerCenter = markerCenter + p;
      EXPECT_NEAR(std::sqrt(dot(p - center, p - center)), 0.025f, 1e-6f);
    }
    near(markerCenter * (1.0f / markerVertices), center);
  }
  b.get<Transform>().position.x = 2;
  EXPECT_TRUE(Detail::captureCollisionDebug(scene, false, false, true, &simulation).contacts.empty());
  scene.destroy(a);
  EXPECT_TRUE(simulation.contactSnapshot(scene).empty());
}

TEST(CollisionDebug, RecordedMidEdgeArtifactDoesNotReturnAfterResetOrMicronEdits)
{
  Scene scene;
  const auto a = add(scene, Box{}, {-0.999984801f, 1.49505603f, 0.0000895329431f});
  const auto b = add(scene, Box{}, {0.000119607488f, 1.49391961f, 0.000273727899f});
  a.get<Transform>().rotation = {1, -0.00000617850765f, -0.00000348690537f, -0.000050568f};
  b.get<Transform>().rotation = {1, -0.000175368259f, -0.000144732257f, 0.0000656225675f};
  a.add<RigidBody>(); b.add<RigidBody>();
  Physics::Config config; scene.physics().fields.clear();
  Physics::System simulation(config);
  for (int variant = 0; variant < 5; ++variant) {
    simulation.reset();
    b.get<Transform>().position.z = 0.000273727899f + (variant - 2) * 0.000001f;
    const auto debug = Detail::captureCollisionDebug(scene, false, false, true, &simulation);
    ASSERT_EQ(debug.contacts.size(), 1u);
    ASSERT_EQ(debug.contacts.front().points.size(), 4u);
    for (const auto& point : debug.contacts.front().points) {
      const float y = (point.pointA.y + point.pointB.y) * 0.5f;
      EXPECT_TRUE(y < 1.01f || y > 1.98f) << "Unstable mid-edge contact at y=" << y;
      const auto& ta = a.get<Transform>(); const auto& tb = b.get<Transform>();
      const auto pa = ta.rotation.conjugate().rotate(point.pointA - ta.position);
      const auto pb = tb.rotation.conjugate().rotate(point.pointB - tb.position);
      for (const auto p : {pa, pb}) {
        EXPECT_LE(std::abs(p.x), 0.500001f); EXPECT_LE(std::abs(p.y), 0.500001f); EXPECT_LE(std::abs(p.z), 0.500001f);
        EXPECT_NEAR(std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}), 0.5f, 1e-6f);
      }
    }
  }
}

TEST(CollisionDebug, ContactPointsAndNormalsHaveDistinctRedAndGreenPixels)
{
  WindowConfig config; config.visible = false; config.width = config.height = 256;
  Window window(config);
  Detail::OpenGLDevice device(window, false);
  Detail::OpenGLDisplay display(device);
  Detail::DebugDraw draw(device);
  Scene scene; camera(scene);
  add(scene, Sphere{0.6f}, {-0.5f, 0, 0});
  add(scene, Sphere{0.6f}, {0.5f, 0, 0});
  auto debug = Detail::captureCollisionDebug(scene, true, true, true);
  ASSERT_EQ(debug.contacts.size(), 1u);
  EXPECT_FALSE(debug.points.empty()); EXPECT_FALSE(debug.normals.empty());
  const auto snapshot = Renderer::SceneSnapshot::capture(scene, Assets{});
  std::vector<unsigned char> pixels(256 * 256 * 4);
  display.begin({256, 256});
  draw.colliders(debug, *snapshot.camera, {256, 256});
  glReadBuffer(GL_BACK); glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  int red = 0, green = 0, yellow = 0, cyan = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    if (pixels[i] > 240 && pixels[i+1] < 10 && pixels[i+2] < 10) ++red;
    if (pixels[i] < 10 && pixels[i+1] > 240 && pixels[i+2] < 10) ++green;
    if (pixels[i] > 240 && pixels[i+1] > 110 && pixels[i+1] < 145 && pixels[i+2] < 10) ++yellow;
    if (pixels[i] < 10 && pixels[i+1] > 240 && pixels[i+2] > 240) ++cyan;
  }
  EXPECT_GT(red, 0); EXPECT_GT(green, 3); EXPECT_GT(yellow, 50); EXPECT_GT(cyan, 50);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(CollisionDebug, FieldAndTotalForceArrowsHaveDistinctBlueAndYellowPixels)
{
  WindowConfig config; config.visible = false; config.width = config.height = 256;
  Window window(config);
  Detail::OpenGLDevice device(window, false);
  Detail::OpenGLDisplay display(device);
  Detail::DebugDraw draw(device);
  Scene scene; camera(scene);
  const auto e = add(scene, Sphere{0.1f}); e.add<RigidBody>();
  scene.physics().fields = {std::make_shared<const Physics::UniformGravity>(Vec3{1, 0, 0})};
  Physics::System simulation; simulation.step(scene, 0.01f);
  const auto debug = Detail::captureCollisionDebug(scene, false, false, false, &simulation, true);
  ASSERT_EQ(debug.fieldForces.size(), 6u); ASSERT_EQ(debug.totalForces.size(), 6u);
  const auto snapshot = Renderer::SceneSnapshot::capture(scene, Assets{});
  display.begin({256, 256}); draw.colliders(debug, *snapshot.camera, {256, 256});
  std::vector<unsigned char> pixels(256 * 256 * 4);
  glReadBuffer(GL_BACK); glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  int blue = 0, yellow = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    if (pixels[i] < 10 && pixels[i + 1] > 50 && pixels[i + 1] < 80 && pixels[i + 2] > 240) ++blue;
    if (pixels[i] > 240 && pixels[i + 1] > 240 && pixels[i + 2] < 10) ++yellow;
  }
  EXPECT_GT(blue, 3); EXPECT_GT(yellow, 3); EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(CollisionDebug, LogsSimulationAndBodyValuesWithoutPerFrameSummarySpam)
{
  struct Restore { ~Restore() { Log::Configure(); } } restore;
  std::vector<Log::Record> records;
  Log::Config logging; logging.console = false;
  logging.sink = [&](const Log::Record& r) { records.push_back(r); };
  Log::Configure(logging);
  auto config = hidden(); config.debug.physics = true;
  config.renderer.render = false; config.renderer.wireframe = true;
  Application app(config);
  auto scene = std::make_shared<Scene>(); camera(*scene);
  add(*scene, Sphere{1}).add<RigidBody>();
  const auto inactive = scene->create<Archetype<Transform, Collider, RigidBody>>();
  inactive.get<Transform>().scale = {};
  inactive.get<RigidBody>().mass = -1;
  app.runtime().setScene(scene);
  app.Run({4});
  int summaries = 0, bodies = 0;
  for (const auto& r : records) if (r.category == "Physics/Debug") {
    if (r.message.find("fixed_step=") != std::string::npos) {
      ++summaries;
      EXPECT_NE(r.message.find("fields="), std::string::npos);
      EXPECT_NE(r.message.find("dynamic=1"), std::string::npos);
      EXPECT_NE(r.message.find("contacts="), std::string::npos);
    }
    if (r.message.find("mass=") != std::string::npos) ++bodies;
  }
  EXPECT_EQ(summaries, 1); EXPECT_EQ(bodies, 1);
}

TEST(CollisionDebug, ColliderEditsAndOverlayTogglesDoNotRestartPathTracing)
{
  auto config = hidden();
  config.debug.physics = true;
  Application app(config);
  const auto scene = std::make_shared<Scene>();
  camera(*scene);
  const auto object = add(*scene, Sphere{1});
  app.runtime().setScene(scene);
  std::uint64_t revision = 0;
  app.on<FrameUpdate>([&](const auto& frame) {
    if (frame.frame == 2) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (app.runtime().renderer().progress().samplesPerPixel != 1 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      ASSERT_EQ(app.runtime().renderer().progress().samplesPerPixel, 1u);
      revision = app.runtime().renderer().progress().revision;
      object.get<Collider>() = Collider{Box{{2, 1, 1}}};
      object.get<Transform>().position.x = 1;
      app.runtime().setPhysicsDebug(false);
    }
    if (frame.frame == 3) {
      EXPECT_EQ(app.runtime().renderer().progress().revision, revision);
      EXPECT_EQ(app.runtime().renderer().progress().samplesPerPixel, 1u);
      app.runtime().setPhysicsDebug(true);
      object.remove<Collider>();
    }
    if (frame.frame == 4)
      EXPECT_EQ(app.runtime().renderer().progress().revision, revision);
  });
  app.Run({4});
  EXPECT_GT(revision, 0u);
}
