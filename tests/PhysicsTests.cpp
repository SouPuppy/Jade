#include <Jade/Jade.hpp>
#include "internal/Renderer/Debug/CollisionDebug.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>

namespace {
using namespace Jade;
using Physical = Archetype<Transform, Collider, RigidBody>;
struct Block { using Components = Physical; };
Entity block(Scene& scene) {
  const auto e = scene.world().create<Block>();
  e.get<Collider>() = Collider{Box{}};
  return e;
}
Entity floor(Scene& scene) {
  const auto e = scene.world().create();
  e.add<Transform>().position = {0, -0.5f, 0};
  e.add<Collider>(Box{{6, 0.5f, 6}});
  return e;
}
double length(Vec3 v) { return std::hypot(v.x, v.y, v.z); }
EngineConfig hidden() {
  EngineConfig c; c.window.visible = false; c.window.width = c.window.height = 64;
  c.renderer.rayTracing.enabled = false; c.renderer.wireframe = true; c.logPerformance = false;
  return c;
}
}
TEST(Physics, GravityAndMassDependentLinearAndAngularImpulses) {
  Scene scene{std::make_shared<World>()};
  const auto a = block(scene), b = block(scene);
  b.get<Transform>().position = {10, 0, 0};
  b.get<RigidBody>().mass = 2;
  Physics::System system;
  system.step(scene.world(), 1.0f / 120);
  EXPECT_NEAR(a.get<RigidBody>().velocity.y, -9.81 / 120, 1e-6);
  EXPECT_FLOAT_EQ(a.get<RigidBody>().velocity.y, b.get<RigidBody>().velocity.y);
  EXPECT_NEAR(a.get<Transform>().position.y, -9.81 / (120 * 120), 1e-7);
  system.applyImpulse(scene.world(), a, {1, 0, 0}, a.get<Transform>().position);
  system.applyImpulse(scene.world(), b, {1, 0, 0}, b.get<Transform>().position);
  EXPECT_FLOAT_EQ(a.get<RigidBody>().velocity.x, 1);
  EXPECT_FLOAT_EQ(b.get<RigidBody>().velocity.x, 0.5f);
  system.applyImpulse(scene.world(), a, {1, 0, 0}, a.get<Transform>().position + Vec3{0, 0.5f, 0});
  EXPECT_NEAR(a.get<RigidBody>().angularVelocity.z, -3, 1e-5);
  system.step(scene.world(), 1.0f / 120);
  EXPECT_LT(a.get<Transform>().rotation.z, 0);
}
TEST(Physics, BoxFaceManifoldHasFourContactsAndSpherePairsHaveOne) {
  Scene scene{std::make_shared<World>()};
  const auto a = block(scene), b = block(scene);
  b.get<Transform>().position.y = 0.9f;
  auto contacts = Physics::manifolds(scene.world());
  ASSERT_EQ(contacts.size(), 1u);
  EXPECT_EQ(contacts[0].points.size(), 4u);
  for (const auto& p : contacts[0].points) EXPECT_NEAR(p.separation, -0.1, 1e-6);
  a.get<Collider>() = Collider{Sphere{0.5f}};
  EXPECT_EQ(Physics::manifolds(scene.world())[0].points.size(), 1u);
  b.get<Collider>() = Collider{Sphere{0.5f}};
  EXPECT_EQ(Physics::manifolds(scene.world())[0].points.size(), 1u);
  b.get<Transform>().position.y = 1.003f;
  EXPECT_TRUE(Physics::manifolds(scene.world()).empty());
  EXPECT_EQ(Physics::manifolds(scene.world(), 0.005f).size(), 1u);
}
TEST(Physics, PyramidRestsForTwentySecondsWithGravityStillEnabled) {
  Scene scene{std::make_shared<World>()};
  const auto ground = floor(scene);
  std::vector<Entity> boxes;
  for (int layer = 0; layer < 3; ++layer) {
    const int width = 5 - 2 * layer;
    for (int i = 0; i < width; ++i) {
      const auto e = block(scene);
      e.get<Transform>().position = {static_cast<float>(i - width / 2), 0.6f + 1.1f * layer, 0};
      boxes.push_back(e);
    }
  }
  JobSystem jobs({3});
  Physics::Config quality; quality.sleeping = false;
  Physics::System system(jobs,quality);
  std::array<Vec3, 9> settled;
  double maxVelocity = 0, maxAngular = 0, maxDrift = 0;
  float maxPenetration = 0;
  for (int step = 0; step < 2400; ++step) {
    system.step(scene.world(), 1.0f / 120);
    if (step == 1799) for (std::size_t i = 0; i < boxes.size(); ++i) settled[i] = boxes[i].get<Transform>().position;
    if (step >= 1800) {
      maxPenetration = std::max(maxPenetration, system.statistics().maximumPenetration);
      for (std::size_t i = 0; i < boxes.size(); ++i) {
        const auto& b = boxes[i].get<RigidBody>(); const auto& p = boxes[i].get<Transform>();
        maxVelocity = std::max(maxVelocity, length(b.velocity));
        maxAngular = std::max(maxAngular, length(b.angularVelocity));
        maxDrift = std::max(maxDrift, length(p.position - settled[i]));
      }
      for (const auto& m : system.contacts()) {
        const auto a = std::find(boxes.begin(), boxes.end(), m.a), b = std::find(boxes.begin(), boxes.end(), m.b);
        if (a == boxes.end() || b == boxes.end()) continue;
        const auto ai = a - boxes.begin(), bi = b - boxes.begin();
        const bool sameRow = (ai < 5 && bi < 5) || (ai >= 5 && ai < 8 && bi >= 5 && bi < 8);
        if (!sameRow || std::abs(m.normal.x) < 0.99f) continue;
        for (const auto& point : m.points) {
          const auto& ta = m.a.get<Transform>(); const auto& tb = m.b.get<Transform>();
          const auto localA = ta.rotation.conjugate().rotate(point.pointA - ta.position);
          const auto localB = tb.rotation.conjugate().rotate(point.pointB - tb.position);
          EXPECT_NEAR(std::abs(localA.y), 0.5f, 0.002f);
          EXPECT_NEAR(std::abs(localB.y), 0.5f, 0.002f);
        }
      }
    }
  }
  EXPECT_LT(maxVelocity, 0.02);
  EXPECT_LT(maxAngular, 0.02);
  EXPECT_LE(maxPenetration, 0.005f);
  EXPECT_LT(maxDrift, 0.01);
  for (std::size_t i = 0; i < boxes.size(); ++i) {
    const auto& p = boxes[i].get<Transform>();
    const float expected = i < 5 ? 0.5f : i < 8 ? 1.5f : 2.5f;
    EXPECT_NEAR(p.position.y, expected, 0.02);
    EXPECT_NEAR(p.position.z, 0, 0.05);
    EXPECT_TRUE(std::isfinite(p.position.x));
    const auto q = p.rotation;
    EXPECT_NEAR(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z, 1, 1e-4);
  }
  EXPECT_FLOAT_EQ(ground.get<Transform>().position.y, -0.5f);
  EXPECT_EQ(system.statistics().dynamicBodies, 9u);
  RecordProperty("maximum_linear_speed", std::to_string(maxVelocity));
  RecordProperty("maximum_angular_speed", std::to_string(maxAngular));
  RecordProperty("maximum_penetration", std::to_string(maxPenetration));
  RecordProperty("maximum_drift", std::to_string(maxDrift));
}
TEST(Physics, SphereSettlesAndZeroMassRemainsStatic) {
  Scene scene{std::make_shared<World>()};
  floor(scene);
  const auto ball = block(scene);
  ball.get<Collider>() = Collider{Sphere{0.5f}};
  ball.get<Transform>().position.y = 2;
  const auto fixed = block(scene);
  fixed.get<Transform>().position = {3, 2, 0}; fixed.get<RigidBody>().mass = 0;
  Physics::System system;
  for (int i = 0; i < 600; ++i) system.step(scene.world(), 1.0f / 120);
  EXPECT_NEAR(ball.get<Transform>().position.y, 0.5, 0.005);
  EXPECT_LT(length(ball.get<RigidBody>().velocity), 0.02);
  EXPECT_FLOAT_EQ(fixed.get<Transform>().position.y, 2);
}
TEST(Physics, ValidationIsTransactionalAndCacheDoesNotSurviveRecycling) {
  Scene scene{std::make_shared<World>()};
  floor(scene);
  const auto a = block(scene), b = block(scene);
  a.get<Transform>().position = {0, 3, 0}; b.get<Transform>().position = {3, 3, 0};
  Physics::System system;
  b.get<RigidBody>().mass = -1;
  EXPECT_THROW(system.step(scene.world(), 0.01f), std::invalid_argument);
  EXPECT_FLOAT_EQ(a.get<Transform>().position.y, 3);
  EXPECT_FLOAT_EQ(a.get<RigidBody>().velocity.y, 0);
  b.get<RigidBody>().mass = 1;
  scene.world().setParent(b, a);
  EXPECT_THROW(system.step(scene.world(), 0.01f), std::invalid_argument);
  scene.world().setParent(b);
  a.get<RigidBody>().velocity.x = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(system.step(scene.world(), 0.01f), std::invalid_argument);
  a.get<RigidBody>().velocity = {};
  system.step(scene.world(), 0.01f);
  scene.world().destroy(a); scene.world().destroy(b);
  const auto fresh = block(scene); fresh.get<Transform>().position.y = 5;
  system.step(scene.world(), 0.01f);
  EXPECT_NEAR(fresh.get<RigidBody>().velocity.y, -0.0981f, 1e-6);
  fresh.remove<Collider>();
  EXPECT_THROW(system.step(scene.world(), 0.01f), std::invalid_argument);
}

TEST(Physics, EmptyColliderSuspendsBodiesAndClearsContactsInSerialAndJobs) {
  for (const unsigned workers : {0u, 1u, 3u}) {
    SCOPED_TRACE(workers);
    std::unique_ptr<JobSystem> jobs;
    if (workers) jobs = std::make_unique<JobSystem>(JobSystemConfig{workers});
    auto system = jobs ? std::make_unique<Physics::System>(*jobs) : std::make_unique<Physics::System>();
    Scene scene{std::make_shared<World>()};
    const auto ground = floor(scene);
    const auto e = scene.world().create<Physical>();
    e.get<Transform>().position.y = 0.5f;
    e.get<RigidBody>().velocity = {0, -1, 0};
    EXPECT_NO_THROW(system->step(scene.world(), 1.0f / 120));
    EXPECT_FLOAT_EQ(e.get<Transform>().position.y, 0.5f);
    EXPECT_EQ(e.get<RigidBody>().velocity, (Vec3{0, -1, 0}));
    EXPECT_EQ(system->statistics().dynamicBodies, 0u);
    EXPECT_EQ(system->statistics().staticBodies, 1u);
    EXPECT_THROW(system->applyImpulse(scene.world(), e, {1, 0, 0}, {}), std::invalid_argument);
    EXPECT_EQ(e.get<RigidBody>().velocity, (Vec3{0, -1, 0}));
    EXPECT_EQ(e.get<RigidBody>().angularVelocity, (Vec3{}));
    e.get<Collider>() = Collider{Box{}};
    system->step(scene.world(), 1.0f / 120);
    EXPECT_EQ(system->statistics().dynamicBodies, 1u);
    ASSERT_FALSE(system->contacts().empty());
    const auto pose = e.get<Transform>();
    const auto velocity = e.get<RigidBody>().velocity;
    const auto angular = e.get<RigidBody>().angularVelocity;
    e.get<Collider>() = {};
    EXPECT_TRUE(system->contactSnapshot(scene.world()).empty());
    EXPECT_FALSE(Physics::contact(scene.world(), e, ground));
    system->step(scene.world(), 1.0f / 120);
    EXPECT_TRUE(system->contacts().empty());
    EXPECT_EQ(system->statistics().dynamicBodies, 0u);
    EXPECT_EQ(e.get<Transform>().position, pose.position);
    EXPECT_EQ(e.get<Transform>().rotation, pose.rotation);
    EXPECT_EQ(e.get<RigidBody>().velocity, velocity);
    EXPECT_EQ(e.get<RigidBody>().angularVelocity, angular);
    e.get<Collider>() = Collider{Sphere{0.5f}};
    e.get<Transform>().position.y = 3;
    e.get<RigidBody>().velocity = {};
    system->step(scene.world(), 1.0f / 120);
    EXPECT_EQ(system->statistics().dynamicBodies, 1u);
    EXPECT_LT(e.get<Transform>().position.y, 3);
    EXPECT_LT(e.get<RigidBody>().velocity.y, 0);
    EXPECT_TRUE(system->contacts().empty());
  }
}

TEST(Physics, EmptyColliderSkipsUnusedDataAndZeroBodyJobGraphs) {
  for (const unsigned workers : {0u, 1u, 3u}) {
    SCOPED_TRACE(workers);
    std::unique_ptr<JobSystem> jobs;
    if (workers) jobs = std::make_unique<JobSystem>(JobSystemConfig{workers});
    auto system = jobs ? std::make_unique<Physics::System>(*jobs) : std::make_unique<Physics::System>();
    Scene scene{std::make_shared<World>()};
    const auto e = scene.world().create<Archetype<Collider, RigidBody>>();
    e.get<RigidBody>().mass = -1;
    EXPECT_NO_THROW(system->step(scene.world(), 0.01f));
    e.add<Transform>().scale = {};
    EXPECT_NO_THROW(system->step(scene.world(), 0.01f));
    EXPECT_EQ(system->statistics().dynamicBodies, 0u);
    EXPECT_EQ(system->statistics().staticBodies, 0u);
    e.get<Collider>() = Collider{Box{}};
    EXPECT_THROW(system->step(scene.world(), 0.01f), std::invalid_argument);
    e.get<RigidBody>().mass = 1;
    EXPECT_THROW(system->step(scene.world(), 0.01f), std::invalid_argument);
    EXPECT_EQ(e.get<RigidBody>().velocity, (Vec3{}));
    e.get<Transform>().scale = {1, 1, 1};
    EXPECT_NO_THROW(system->step(scene.world(), 0.01f));
    EXPECT_EQ(system->statistics().dynamicBodies, 1u);
  }
}

TEST(Physics, RestitutionAndFrictionChangeContactResponse) {
  Scene scene{std::make_shared<World>()}; floor(scene);
  const auto ball = block(scene);
  ball.get<Collider>() = Collider{Sphere{0.5f}};
  ball.get<Transform>().position.y = 1;
  ball.get<RigidBody>().velocity.y = -2;
  ball.get<RigidBody>().restitution = 0.8f;
  Physics::System system;
  float upward = 0;
  for (int i = 0; i < 120; ++i) {
    system.step(scene.world(), 1.0f/120);
    upward = std::max(upward, ball.get<RigidBody>().velocity.y);
  }
  EXPECT_GT(upward, 1);
  const auto slide = [](float friction) {
    Scene s{std::make_shared<World>()}; floor(s); const auto box = block(s);
    box.get<Transform>().position.y = 0.5f;
    box.get<RigidBody>().velocity.x = 2;
    box.get<RigidBody>().friction = friction;
    Physics::System physics;
    for (int i = 0; i < 120; ++i) physics.step(s.world(), 1.0f/120);
    return box.get<RigidBody>().velocity.x;
  };
  EXPECT_NEAR(slide(0), 2, 0.01);
  EXPECT_LT(std::abs(slide(0.6f)), 0.1);
}
TEST(Runtime, FixedClockIsFrameRateIndependentAndDropsExcessDebt) {
  const auto simulate = [](float dt, int frames) {
    Application app(hidden());
    const auto scene = std::make_shared<Scene>(std::make_shared<World>());
    const auto e = block(*scene);
    e.get<Transform>().position.y = 10;
    app.runtime().setScene(scene);
    for (int i = 0; i < frames; ++i) app.runtime().tick(dt);
    EXPECT_EQ(app.runtime().physics().statistics().steps, 120u);
    EXPECT_EQ(app.runtime().droppedTime(), 0);
    return e.get<Transform>().position;
  };
  const auto a = simulate(1.0f/30, 30), b = simulate(1.0f/60, 60);
  EXPECT_EQ(a, b);
  Application app(hidden());
  auto scene = std::make_shared<Scene>(std::make_shared<World>()); block(*scene); app.runtime().setScene(scene);
  app.runtime().tick(1);
  EXPECT_EQ(app.runtime().substeps(), 8u);
  EXPECT_GT(app.runtime().droppedTime(), 0.9);
  app.runtime().setScene(std::make_shared<Scene>(std::make_shared<World>()));
  app.runtime().tick(0);
  EXPECT_EQ(app.runtime().physics().statistics().steps, 0u);
}

TEST(Runtime, PauseAndRunBoundariesDoNotAccumulateOldTime) {
  Application app(hidden());
  auto scene = std::make_shared<Scene>(std::make_shared<World>()); const auto e = block(*scene);
  e.get<Transform>().position.y = 10; app.runtime().setScene(scene);
  app.runtime().tick(1.0f/120);
  const auto position = e.get<Transform>().position;
  app.runtime().setPaused(true); app.runtime().tick(10);
  EXPECT_EQ(e.get<Transform>().position, position);
  EXPECT_EQ(app.runtime().substeps(), 0u);
  app.runtime().setPaused(false); app.runtime().tick(0);
  EXPECT_EQ(e.get<Transform>().position, position);
  app.Run({1}); app.Run({1});
  EXPECT_EQ(e.get<Transform>().position, position);
  app.runtime().tick(1.0f/120);
  EXPECT_LT(e.get<Transform>().position.y, position.y);
}

TEST(Runtime, PausedDebugPreservesSolvedContactsAndRefreshesSceneEdits) {
  Application app(hidden());
  auto scene = std::make_shared<Scene>(std::make_shared<World>()); floor(*scene);
  const auto e = block(*scene); e.get<Transform>().position.y = 0.5f;
  app.runtime().setScene(scene); app.runtime().tick(1.0f / 120);
  auto& physics = app.runtime().physics();
  ASSERT_EQ(physics.contacts().size(), 1u);
  const auto before = physics.contactSnapshot(scene->world());
  ASSERT_EQ(before.size(), 1u);
  app.runtime().setPaused(true); app.runtime().tick(10);
  const auto debug = Detail::captureCollisionDebug(scene->world(), false, false, true, &physics);
  ASSERT_EQ(debug.contacts.size(), 1u);
  ASSERT_EQ(debug.contacts.front().points.size(), before.front().points.size());
  for (std::size_t i = 0; i < before.front().points.size(); ++i) {
    EXPECT_EQ(debug.contacts.front().points[i].pointA, before.front().points[i].pointA);
    EXPECT_EQ(debug.contacts.front().points[i].pointB, before.front().points[i].pointB);
    EXPECT_FLOAT_EQ(debug.contacts.front().points[i].normalImpulse, before.front().points[i].normalImpulse);
  }
  e.get<Transform>().position.y = 10;
  EXPECT_TRUE(physics.contactSnapshot(scene->world()).empty());
  app.runtime().setPaused(false); app.runtime().tick(1.0f / 120);
  EXPECT_TRUE(physics.contacts().empty());
}
