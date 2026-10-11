#include <Jade/Jade.hpp>
#include "internal/Physics/BroadPhase.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <limits>
#include <random>

using namespace Jade;
namespace {
struct Stacks {
  Scene scene;
  Entity ground;
  std::vector<Entity> bodies;
  Stacks() {
    ground = scene.create(); ground.add<Transform>().position = {0, -0.5f, 0};
    ground.add<Collider>(Box{{20, 0.5f, 20}});
    for (float x : {-4.0f, 4.0f}) for (int level = 0; level < 3; ++level) {
      auto e = scene.create(); e.add<Transform>().position = {x, 0.5f + level, 0};
      e.add<Collider>(Box{{0.5f, 0.5f, 0.5f}}); e.add<RigidBody>(); bodies.push_back(e);
    }
  }
};
void compare(const Stacks& a, const Stacks& b, const Physics::System& sa, const Physics::System& sb) {
  for (std::size_t i = 0; i < a.bodies.size(); ++i) {
    const auto& ta = a.bodies[i].get<Transform>(); const auto& tb = b.bodies[i].get<Transform>();
    EXPECT_EQ(ta.position, tb.position); EXPECT_EQ(ta.rotation, tb.rotation);
    EXPECT_EQ(a.bodies[i].get<RigidBody>().velocity, b.bodies[i].get<RigidBody>().velocity);
    EXPECT_EQ(a.bodies[i].get<RigidBody>().angularVelocity, b.bodies[i].get<RigidBody>().angularVelocity);
  }
  ASSERT_EQ(sa.contacts().size(), sb.contacts().size());
  for (std::size_t i = 0; i < sa.contacts().size(); ++i) {
    const auto& ca = sa.contacts()[i]; const auto& cb = sb.contacts()[i];
    EXPECT_EQ(ca.a.id(), cb.a.id()); EXPECT_EQ(ca.b.id(), cb.b.id()); EXPECT_EQ(ca.normal, cb.normal);
    ASSERT_EQ(ca.points.size(), cb.points.size());
    for (std::size_t j = 0; j < ca.points.size(); ++j) {
      EXPECT_EQ(ca.points[j].pointA, cb.points[j].pointA); EXPECT_EQ(ca.points[j].pointB, cb.points[j].pointB);
      EXPECT_EQ(ca.points[j].normalImpulse, cb.points[j].normalImpulse);
      EXPECT_EQ(ca.points[j].frictionImpulse, cb.points[j].frictionImpulse);
    }
  }
}
} // namespace

TEST(JobPhysics, SweepAndPruneMatchesIndependentAllPairsIncludingStaticQueries) {
  JobSystem jobs({3});
  std::mt19937 random(73); std::uniform_real_distribution<double> position(-8, 8), size(0.01, 2);
  std::vector<Detail::WorldCollider> shapes(400);
  std::vector<unsigned char> dynamic(shapes.size());
  for (std::size_t i = 0; i < shapes.size(); ++i) {
    const Detail::CollisionVector center{position(random), position(random), position(random)};
    const Detail::CollisionVector extent{size(random), size(random), size(random)};
    shapes[i].bounds = {center - extent, center + extent}; dynamic[i] = i % 3 != 0;
  }
  // Equal minima, exact touching and overlapping static/static pairs.
  shapes[0].bounds = {{0, 0, 0}, {1, 1, 1}};
  shapes[1].bounds = {{1, 0, 0}, {2, 1, 1}};
  shapes[3].bounds = shapes[0].bounds;
  for (const double margin : {0.0, 0.005}) for (bool filter : {false, true}) {
    std::vector<Detail::ColliderPair> expected;
    for (std::size_t i = 0; i < shapes.size(); ++i) for (std::size_t j = i + 1; j < shapes.size(); ++j)
      if ((!filter || dynamic[i] || dynamic[j]) && shapes[i].bounds.overlaps(shapes[j].bounds, margin))
        expected.push_back({i, j});
    const std::span<const unsigned char> mask = filter ? std::span<const unsigned char>{dynamic} : std::span<const unsigned char>{};
    EXPECT_EQ(Detail::broadPhase(shapes, margin, nullptr, mask), expected);
    EXPECT_EQ(Detail::broadPhase(shapes, margin, &jobs, mask), expected);
  }
}

TEST(JobPhysics, SerialOneAndThreeWorkersMatchAcrossSharedGroundIslandsAndEdits) {
  Stacks serial, single, parallel;
  JobSystem one({1}), many({3});
  Physics::System a, b(one), c(many);
  for (int step = 0; step < 600; ++step) {
    if (step == 180) {
      for (auto [fixture, system] : {std::pair{&serial, &a}, std::pair{&single, &b}, std::pair{&parallel, &c}}) {
        const auto e = fixture->bodies.back();
        system->applyImpulse(fixture->scene, e, {0.05f, 0, 0}, e.get<Transform>().position + Vec3{0, 0.2f, 0});
      }
    }
    a.step(serial.scene, 1.0f / 120); b.step(single.scene, 1.0f / 120); c.step(parallel.scene, 1.0f / 120);
    if (step % 30 == 0) { compare(serial, single, a, b); compare(serial, parallel, a, c); }
  }
  compare(serial, single, a, b); compare(serial, parallel, a, c);
  EXPECT_EQ(c.statistics().islands, 2u);
  EXPECT_EQ(parallel.ground.get<Transform>().position, (Vec3{0, -0.5f, 0}));
}

TEST(JobPhysics, WorkerFailureDoesNotCommitAndEntityRecyclingRemainsSafe) {
  JobSystem jobs({2}); Stacks fixture; Physics::System physics(jobs);
  physics.step(fixture.scene, 1.0f / 120);
  const auto previous = fixture.bodies[0].get<Transform>();
  const auto stats = physics.statistics();
  fixture.bodies.back().get<RigidBody>().velocity.x = std::numeric_limits<float>::max();
  EXPECT_THROW(physics.step(fixture.scene, 2), std::invalid_argument);
  EXPECT_EQ(fixture.bodies[0].get<Transform>().position, previous.position);
  EXPECT_EQ(physics.statistics().steps, stats.steps);
  const auto old = fixture.bodies.back(); fixture.scene.destroy(old);
  const auto fresh = fixture.scene.create(); fresh.add<Transform>().position = {8, 5, 0};
  fresh.add<Collider>(Sphere{0.5f}); fresh.add<RigidBody>();
  physics.step(fixture.scene, 1.0f / 120);
  EXPECT_NEAR(fresh.get<RigidBody>().velocity.y, -9.81f / 120, 1e-6f);
}

TEST(JobPhysics, SharedSchedulerKeepsPhysicsAndRayTracingDeterministic) {
  Stacks fixture; JobSystem jobs({3}); Physics::Config config; config.parallelThreshold = 1; Physics::System physics(jobs,config);
  Assets assets;
  const auto camera = fixture.scene.create(); camera.add<Transform>().position = {0, 2, 10}; camera.add<Camera>();
  fixture.scene.setCamera(camera);
  auto light = fixture.scene.create(); light.add<Transform>();
  light.add<Geometry>(assets.add(MeshData::sphere(1, 16, 8)));
  light.add<Material>().emission = {4, 2, 1};
  const auto snapshot = Renderer::SceneSnapshot::capture(fixture.scene, assets);
  const auto rayScene = Renderer::RayScene::build(snapshot);
  Renderer::RayTracing renderer(jobs), reference(jobs);
  Renderer::Film film({32, 32}), expected({32, 32});
  const Renderer::RenderRequest request{*snapshot.camera, film.size(), 0, 123, {}};
  auto rendering = jobs.submit([&] { renderer.render(rayScene, request, film); }, JobCategory::RayTracing);
  for (int i = 0; i < 20; ++i) physics.step(fixture.scene, 1.0f / 120);
  jobs.wait(rendering); reference.render(rayScene, request, expected);
  EXPECT_EQ(film.resolve().pixels, expected.resolve().pixels);
  const auto stats = jobs.statistics();
  EXPECT_GT(stats.categories[static_cast<std::size_t>(JobCategory::Physics)].completed, 0u);
  EXPECT_GT(stats.categories[static_cast<std::size_t>(JobCategory::RayTracing)].completed, 0u);
}

TEST(JobPhysics, RecordsSparseBroadPhaseScalingWithoutTimingThresholds) {
  JobSystem jobs({3});
  for (const std::size_t count : {1000u, 10000u}) {
    std::vector<Detail::WorldCollider> shapes(count);
    for (std::size_t i = 0; i < count; ++i) {
      const double x = static_cast<double>(i) * 3;
      shapes[i].bounds = {{x, 0, 0}, {x + 1, 1, 1}};
    }
    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(Detail::broadPhase(shapes, 0, &jobs).empty());
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    RecordProperty("sparse_" + std::to_string(count) + "_milliseconds", std::to_string(ms));
  }
}
