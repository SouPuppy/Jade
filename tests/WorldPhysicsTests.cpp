#include <Jade/Jade.hpp>
#include <gtest/gtest.h>
#include <limits>

using namespace Jade;
using namespace Jade::Physics;
namespace {
double norm(Vector p) { return std::hypot(p.x,p.y,p.z); }
BodyDescription sphere(Vector p = {}) { BodyDescription d; d.shape = Collider{Sphere{.5f}}; d.position = p; return d; }
class StageFailure final : public ForceField {
public:
  Vec3 sample(const FieldContext&) const override { return {}; }
  FieldSample sampleStage(const FieldContext& c) const override {
    if (c.time > 0) throw std::runtime_error("second stage"); return {};
  }
};
}
TEST(WorldPhysics, IDsAreWorldScopedGenerationalAndQueriesAreFresh) {
  World a,b; a.setFields({}); const auto first = a.create(sphere({2,0,0}));
  EXPECT_THROW(b.state(first),std::invalid_argument);
  auto ray = a.rayCast({0,0,0},{4,0,0}); ASSERT_TRUE(ray); EXPECT_EQ(ray->body,first); EXPECT_NEAR(ray->fraction,.375,1e-8);
  auto cast = a.shapeCast(sphere(),{4,0,0}); ASSERT_TRUE(cast); EXPECT_NEAR(cast->fraction,.25,1e-7);
  EXPECT_EQ(a.overlap(sphere({2,0,0})),std::vector<BodyID>{first});
  a.teleport(first,{8,0,0},{}); EXPECT_FALSE(a.rayCast({0,0,0},{4,0,0}));
  a.destroy(first); const auto next = a.create(sphere()); EXPECT_EQ(next.slot,first.slot); EXPECT_NE(next.generation,first.generation);
  EXPECT_THROW(a.applyImpulse(first,{1,0,0},{}),std::invalid_argument);
}
TEST(WorldPhysics, PublishedSnapshotsRetainIDsAndSupportConcurrentQueries) {
  World world; world.setFields({}); const auto id = world.create(sphere({2,0,0})); const auto snapshot = world.snapshot();
  world.destroy(id); world.create(sphere({8,0,0}));
  JobSystem jobs({3}); jobs.wait(jobs.parallelFor(1024,[&](auto) {
    const auto hit = snapshot.rayCast({0,0,0},{4,0,0}); ASSERT_TRUE(hit); EXPECT_EQ(hit->body,id);
    EXPECT_EQ(snapshot.state(id).position.x,2); QueryFilter filter; filter.ignored = id; EXPECT_FALSE(snapshot.rayCast({0,0,0},{4,0,0},filter));
  }));
}
TEST(WorldPhysics, ForcesTorqueAndFailedVerletAreTransactional) {
  World w; w.setFields({std::make_shared<StageFailure>()}); w.setIntegration(Integration::VelocityVerlet);
  const auto id = w.create(sphere()); w.addForce(id,{2,0,0},{0,0,1});
  EXPECT_THROW(w.step(.01),std::runtime_error); EXPECT_EQ(w.statistics().steps,0u); EXPECT_EQ(w.time(),0); EXPECT_EQ(w.state(id).position,Vector{});
  w.setFields({}); w.step(.01);
  EXPECT_NEAR(w.state(id).velocity.x,.02,1e-10); EXPECT_NEAR(w.state(id).angularVelocity.z,.1,1e-9);
  w.step(.01); EXPECT_NEAR(w.state(id).velocity.x,.02,1e-10); // one-step force was consumed once
  w.setPersistentForce(id,{3,0,0}); w.step(.01); w.step(.01); EXPECT_NEAR(w.state(id).velocity.x,.08,1e-9);
}
TEST(WorldPhysics, FreeAsymmetricRotationPreservesAngularMomentum) {
  Config c; c.sleeping = false; World w(c); w.setFields({});
  BodyDescription d; d.shape = Collider{Box{{.5f,1,1.5f}}}; d.angularVelocity = {1,2,3}; const auto id = w.create(d);
  const auto initial = w.state(id).angularMomentum;
  for (unsigned i = 0; i < 1200; ++i) w.step(1.0/120);
  EXPECT_LT(norm(w.state(id).angularMomentum-initial)/norm(initial),2e-4);
  EXPECT_GT(norm(w.state(id).angularVelocity-d.angularVelocity),.1);
}
TEST(WorldPhysics, SleepingBodiesWakeWhenTheirSupportIsRemoved) {
  World w; BodyDescription floor; floor.motion = MotionType::Static; floor.position = {0,-.5,0}; floor.shape = Collider{Box{{5,.5f,5}}};
  const auto ground = w.create(floor), ball = w.create(sphere({0,.5,0}));
  for (unsigned i = 0; i < 240; ++i) w.step(1.0/120);
  EXPECT_TRUE(w.state(ball).sleeping); EXPECT_EQ(w.forces(ball).status,ForceStatus::SleepingEquilibrium);
  w.destroy(ground); w.step(1.0/120); EXPECT_FALSE(w.state(ball).sleeping); EXPECT_LT(w.state(ball).velocity.y,-.08);
}
TEST(WorldPhysics, ContinuousSphereStopsAtThinWallAndUsesOtherBodyMotion) {
  Config c; c.sleeping = false; World w(c); w.setFields({});
  BodyDescription wall; wall.motion = MotionType::Static; wall.shape = Collider{Box{{.01f,4,4}}}; wall.position = {0,0,0}; w.create(wall);
  auto d = sphere({-5,0,0}); d.velocity = {1000,0,0}; d.collision = CollisionMode::Continuous;
  const auto ball = w.create(d); w.step(.01);
  EXPECT_LT(w.state(ball).position.x,-.49); EXPECT_LT(std::abs(w.state(ball).velocity.x),1e-5); EXPECT_GT(w.statistics().toiEvents,0u);
  World moving(c); moving.setFields({}); auto a = sphere({-3,0,0}), b = sphere({3,0,0}); a.velocity = {500,0,0}; b.velocity = {-500,0,0}; a.collision = CollisionMode::Continuous;
  const auto ia = moving.create(a), ib = moving.create(b); moving.step(.01);
  EXPECT_LE(moving.state(ia).position.x,moving.state(ib).position.x); EXPECT_NEAR(moving.state(ia).velocity.x,0,1e-5);
}
TEST(WorldPhysics, KinematicTargetsFilterMasksAndFieldLifetime) {
  World w; w.setFields({}); BodyDescription d; d.motion = MotionType::Kinematic; d.category = 2;
  const auto id = w.create(d); w.setKinematicTarget(id,{1,0,0},Quaternion::angleAxis({0,1,0},90),.1);
  for (unsigned i = 0; i < 12; ++i) w.step(1.0/120);
  EXPECT_NEAR(w.state(id).position.x,1,1e-8); EXPECT_LT(norm(w.state(id).velocity),1e-8);
  EXPECT_TRUE(w.query({{-2,-2,-2},{2,2,2}},1).empty()); EXPECT_EQ(w.query({{-2,-2,-2},{2,2,2}},2).size(),1u);
  const auto f = w.addField(std::make_shared<UniformGravity>()); w.removeField(f); EXPECT_THROW(w.removeField(f),std::invalid_argument);
}
TEST(WorldPhysics, HeavyFieldsReallyExecuteOnMultipleWorkers) {
  JobSystem jobs({3}); Config c; c.sleeping = false; World w(jobs,c); w.setFields({}); w.reserve(4096);
  std::vector<BodyID> sources;
  for (unsigned i = 0; i < 4096; ++i) {
    auto d = sphere({double(i%64)*4,double(i/64)*4,0}); const auto id = w.create(d); if (i < 64) sources.push_back(id);
  }
  w.setFields({std::make_shared<BodyGravity>(sources,1,.1)});
  w.step(1.0/120); const auto stats = jobs.statistics(); unsigned executing = 0;
  for (const auto& worker : stats.workers) if (worker.categoryBusyNanoseconds[static_cast<unsigned>(JobCategory::Physics)] > 0) ++executing;
  EXPECT_EQ(executing,3u); EXPECT_GE(stats.peakRunningWorkers,2u);
  EXPECT_GT(w.statistics().timing.fields,0); EXPECT_EQ(w.statistics().dynamicBodies,4096u);
}
TEST(WorldPhysics, BigContactIslandUsesWorkersAndAllSolverBackends) {
  for (const auto backend : {SolverBackend::Scalar,SolverBackend::SSE2,SolverBackend::AVX2}) {
    JobSystem jobs({3}); Config c; c.sleeping = false; c.parallelThreshold = 1; c.backend = backend; World w(jobs,c); w.setFields({});
    for (unsigned i = 0; i < 256; ++i) { auto d = sphere({double(i),0,0}); if (i == 0) d.velocity = {1,0,0}; w.create(d); }
    w.step(1.0/120); EXPECT_EQ(w.statistics().islands,1u); EXPECT_GT(w.statistics().colors,1u);
    unsigned busy = 0; for (const auto& worker : jobs.statistics().workers) if (worker.tasks) ++busy; EXPECT_GE(busy,2u);
  }
}
