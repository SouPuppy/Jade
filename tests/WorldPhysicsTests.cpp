#include <Jade/Jade.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <atomic>

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
class SpinField final : public ForceField {
public:
  std::shared_ptr<std::atomic<unsigned>> preparations = std::make_shared<std::atomic<unsigned>>(0);
  Kernel prepare(std::span<const FieldBody>,std::span<const std::pair<std::uint64_t,std::size_t>>) const override {
    ++*preparations;
    return [](const FieldContext& c) {
      EXPECT_TRUE(std::isfinite(c.body.angularMomentum.z)); EXPECT_TRUE(std::isfinite(c.body.rotation.w));
      return FieldSample{{},{0,0,1+c.time}};
    };
  }
};
}
TEST(WorldPhysics, IDsAreWorldScopedGenerationalAndQueriesAreFresh) {
  Physics::World a,b; a.setFields({}); const auto first = a.create(sphere({2,0,0}));
  EXPECT_THROW(b.state(first),std::invalid_argument);
  auto ray = a.rayCast({0,0,0},{4,0,0}); ASSERT_TRUE(ray); EXPECT_EQ(ray->body,first); EXPECT_NEAR(ray->fraction,.375,1e-8);
  auto cast = a.shapeCast(sphere(),{4,0,0}); ASSERT_TRUE(cast); EXPECT_NEAR(cast->fraction,.25,1e-7);
  EXPECT_EQ(a.overlap(sphere({2,0,0})),std::vector<BodyID>{first});
  a.teleport(first,{8,0,0},{}); EXPECT_FALSE(a.rayCast({0,0,0},{4,0,0}));
  a.destroy(first); const auto next = a.create(sphere()); EXPECT_EQ(next.slot,first.slot); EXPECT_NE(next.generation,first.generation);
  EXPECT_THROW(a.applyImpulse(first,{1,0,0},{}),std::invalid_argument);
}
TEST(WorldPhysics, PublishedSnapshotsRetainIDsAndSupportConcurrentQueries) {
  Physics::World world; world.setFields({}); const auto id = world.create(sphere({2,0,0})); const auto snapshot = world.snapshot();
  world.destroy(id); world.create(sphere({8,0,0}));
  JobSystem jobs({3}); jobs.wait(jobs.parallelFor(1024,[&](auto) {
    const auto hit = snapshot.rayCast({0,0,0},{4,0,0}); ASSERT_TRUE(hit); EXPECT_EQ(hit->body,id);
    EXPECT_EQ(snapshot.state(id).position.x,2); QueryFilter filter; filter.ignored = id; EXPECT_FALSE(snapshot.rayCast({0,0,0},{4,0,0},filter));
  }));
}
TEST(WorldPhysics, ForcesTorqueAndFailedVerletAreTransactional) {
  Physics::World w; w.setFields({std::make_shared<StageFailure>()}); w.setIntegration(Integration::VelocityVerlet);
  const auto id = w.create(sphere()); w.addForce(id,{2,0,0},{0,0,1});
  EXPECT_THROW(w.step(.01),std::runtime_error); EXPECT_EQ(w.statistics().steps,0u); EXPECT_EQ(w.time(),0); EXPECT_EQ(w.state(id).position,Vector{});
  w.setFields({}); w.step(.01);
  EXPECT_NEAR(w.state(id).velocity.x,.02,1e-10); EXPECT_NEAR(w.state(id).angularVelocity.z,.1,1e-9);
  w.step(.01); EXPECT_NEAR(w.state(id).velocity.x,.02,1e-10); // one-step force was consumed once
  w.setPersistentForce(id,{3,0,0}); w.step(.01); w.step(.01); EXPECT_NEAR(w.state(id).velocity.x,.08,1e-9);
}

TEST(WorldPhysics, StageOnlyTorqueFieldsReceiveFullStateAndPrepareOncePerStage) {
  Physics::World w; const auto field = std::make_shared<SpinField>(); w.setFields({field}); const auto id = w.create(sphere());
  w.step(.01); EXPECT_EQ(field->preparations->load(),1u); EXPECT_NEAR(w.state(id).angularVelocity.z,.1,1e-10);
  w.setIntegration(Integration::VelocityVerlet); w.step(.01);
  EXPECT_EQ(field->preparations->load(),3u); EXPECT_NEAR(w.state(id).angularVelocity.z,.2015,1e-9);
  EXPECT_NEAR(w.forces(id).fieldTorque.z,1.015,1e-10);
}
TEST(WorldPhysics, BridgeForcePreviewRetainsNativeSourcesAndInvalidatesAcrossBodies) {
  Jade::World scene; scene.physics().fields.clear(); System system;
  const auto target = scene.create(); target.add<Transform>(); target.add<Collider>(Sphere{.1f}); target.add<RigidBody>();
  const auto sourceEntity = scene.create(); sourceEntity.add<Transform>().position = {4,0,0}; sourceEntity.add<Collider>(Sphere{.1f}); sourceEntity.add<RigidBody>().mass = 10;
  system.synchronize(scene); auto& native = scene.physicsWorld();
  auto sourceDescription = sphere({6,0,0}); sourceDescription.mass = 10; sourceDescription.shape = Collider{Sphere{.1f}};
  const auto source = native.create(sourceDescription);
  native.setFields({std::make_shared<BodyGravity>(std::vector<BodyID>{source,scene.physicsBody(sourceEntity)},1,.1)});
  system.advance(scene,.001); system.publish(scene);
  const auto findTarget = [&](const auto& snapshot) -> BodyForceSnapshot {
    for (const auto& f : snapshot) if (f.entity == target) return f;
    ADD_FAILURE() << "Target force was omitted"; return {};
  };
  auto force = findTarget(system.forceSnapshot(scene)); EXPECT_TRUE(force.solved); EXPECT_GT(force.fieldForce.x,.8f);
  const auto previous = native.state(scene.physicsBody(sourceEntity)).position;
  native.teleport(source,{-6,0,0},{}); sourceEntity.get<Transform>().position = {-4,0,0};
  force = findTarget(system.forceSnapshot(scene)); EXPECT_FALSE(force.solved); EXPECT_LT(force.fieldForce.x,-.8f); EXPECT_EQ(force.totalForce,Vec3{});
  EXPECT_EQ(native.state(scene.physicsBody(sourceEntity)).position,previous); // preview does not issue commands
  system.reset(); EXPECT_NO_THROW(system.forceSnapshot(scene));
}
TEST(WorldPhysics, SolvedForceInspectionDoesNotPrepareAnotherFieldStage) {
  Jade::World scene; const auto field = std::make_shared<SpinField>(); scene.physics().fields = {field};
  const auto e = scene.create(); e.add<Transform>(); e.add<Collider>(Sphere{.5f}); e.add<RigidBody>();
  const auto floor = scene.create(); floor.add<Transform>().position = {0,-10,0}; floor.add<Collider>(Box{{5,.5f,5}}); floor.add<RigidBody>().motion = MotionType::Static;
  System system; system.step(scene,.01f); const auto preparations = field->preparations->load();
  const auto forces = system.forceSnapshot(scene); ASSERT_EQ(forces.size(),1u); EXPECT_TRUE(forces[0].solved);
  EXPECT_EQ(field->preparations->load(),preparations);
}
TEST(WorldPhysics, FreeAsymmetricRotationPreservesAngularMomentum) {
  Config c; c.sleeping = false; Physics::World w(c); w.setFields({});
  BodyDescription d; d.shape = Collider{Box{{.5f,1,1.5f}}}; d.angularVelocity = {1,2,3}; const auto id = w.create(d);
  const auto initial = w.state(id).angularMomentum;
  const double initialEnergy = (initial.x*d.angularVelocity.x+initial.y*d.angularVelocity.y+initial.z*d.angularVelocity.z)*.5;
  for (unsigned i = 0; i < 1200; ++i) w.step(1.0/120);
  EXPECT_LT(norm(w.state(id).angularMomentum-initial)/norm(initial),1e-9);
  const auto state = w.state(id); const double energy = (state.angularMomentum.x*state.angularVelocity.x+state.angularMomentum.y*state.angularVelocity.y+state.angularMomentum.z*state.angularVelocity.z)*.5;
  EXPECT_LT(std::abs(energy-initialEnergy)/initialEnergy,1e-3);
  EXPECT_GT(norm(w.state(id).angularVelocity-d.angularVelocity),.1);
}
TEST(WorldPhysics, SleepingBodiesWakeWhenTheirSupportIsRemoved) {
  Physics::World w; BodyDescription floor; floor.motion = MotionType::Static; floor.position = {0,-.5,0}; floor.shape = Collider{Box{{5,.5f,5}}};
  const auto ground = w.create(floor), ball = w.create(sphere({0,.5,0}));
  for (unsigned i = 0; i < 240; ++i) w.step(1.0/120);
  EXPECT_TRUE(w.state(ball).sleeping); EXPECT_EQ(w.forces(ball).status,ForceStatus::SleepingEquilibrium);
  w.destroy(ground); w.step(1.0/120); EXPECT_FALSE(w.state(ball).sleeping); EXPECT_LT(w.state(ball).velocity.y,-.08);
}

TEST(WorldPhysics, DistantStaticEditsKeepIndependentSleepingIslandsAsleep) {
  Physics::World w; BodyDescription floor; floor.motion = MotionType::Static; floor.position = {0,-.5,0}; floor.shape = Collider{Box{{5,.5f,5}}}; w.create(floor);
  auto distant = floor; distant.position.x = 100; const auto other = w.create(distant),ball = w.create(sphere({0,.5,0}));
  for (unsigned i = 0; i < 120; ++i) w.step(1.0/120);
  ASSERT_TRUE(w.state(ball).sleeping);
  w.teleport(other,{110,-.5,0},{}); w.step(1.0/120); EXPECT_TRUE(w.state(ball).sleeping);
  w.destroy(other); w.step(1.0/120); EXPECT_TRUE(w.state(ball).sleeping);
  auto inserted = floor; inserted.position = {0,.6,0}; inserted.shape = Collider{Box{{.1f,.1f,.1f}}}; w.create(inserted);
  w.step(1.0/120); EXPECT_FALSE(w.state(ball).sleeping);
}

TEST(WorldPhysics, MovingKinematicSupportWakesItsContactIsland) {
  Physics::World w; BodyDescription platform; platform.motion = MotionType::Kinematic; platform.position = {0,-.5,0}; platform.shape = Collider{Box{{5,.5f,5}}};
  const auto floor = w.create(platform),ball = w.create(sphere({0,.5,0}));
  for (unsigned i = 0; i < 120; ++i) w.step(1.0/120);
  ASSERT_TRUE(w.state(ball).sleeping);
  w.setKinematicTarget(floor,{0,-1.5,0},{},1); w.step(1.0/120);
  EXPECT_FALSE(w.state(ball).sleeping); EXPECT_LT(w.state(ball).velocity.y,-.08);
}
TEST(WorldPhysics, ContinuousSphereStopsAtThinWallAndUsesOtherBodyMotion) {
  Config c; c.sleeping = false; Physics::World w(c); w.setFields({});
  BodyDescription wall; wall.motion = MotionType::Static; wall.shape = Collider{Box{{.01f,4,4}}}; wall.position = {0,0,0}; w.create(wall);
  auto d = sphere({-5,0,0}); d.velocity = {1000,0,0}; d.collision = CollisionMode::Continuous;
  const auto ball = w.create(d); w.step(.01);
  EXPECT_LT(w.state(ball).position.x,-.49); EXPECT_LT(std::abs(w.state(ball).velocity.x),1e-5); EXPECT_GT(w.statistics().toiEvents,0u);
  Physics::World moving(c); moving.setFields({}); auto a = sphere({-3,0,0}), b = sphere({3,0,0}); a.velocity = {500,0,0}; b.velocity = {-500,0,0}; a.collision = CollisionMode::Continuous;
  const auto ia = moving.create(a), ib = moving.create(b); moving.step(.01);
  EXPECT_LE(moving.state(ia).position.x,moving.state(ib).position.x); EXPECT_NEAR(moving.state(ia).velocity.x,0,1e-5);
}
TEST(WorldPhysics, KinematicTargetsFilterMasksAndFieldLifetime) {
  Physics::World w; w.setFields({}); BodyDescription d; d.motion = MotionType::Kinematic; d.category = 2;
  const auto id = w.create(d); w.setKinematicTarget(id,{1,0,0},Quaternion::angleAxis({0,1,0},90),.1);
  for (unsigned i = 0; i < 12; ++i) w.step(1.0/120);
  EXPECT_NEAR(w.state(id).position.x,1,1e-8); EXPECT_LT(norm(w.state(id).velocity),1e-8);
  EXPECT_TRUE(w.query({{-2,-2,-2},{2,2,2}},1).empty()); EXPECT_EQ(w.query({{-2,-2,-2},{2,2,2}},2).size(),1u);
  const auto f = w.addField(std::make_shared<UniformGravity>()); w.removeField(f); EXPECT_THROW(w.removeField(f),std::invalid_argument);
}
TEST(WorldPhysics, HeavyFieldsReallyExecuteOnMultipleWorkers) {
  JobSystem jobs({3}); Config c; c.sleeping = false; Physics::World w(jobs,c); w.setFields({}); w.reserve(4096);
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
    JobSystem jobs({3}); Config c; c.sleeping = false; c.parallelThreshold = 1; c.backend = backend; Physics::World w(jobs,c); w.setFields({});
    // A 256-body island can finish entirely on the helping caller before a
    // newly started worker is scheduled. Use enough work to measure workers.
    for (unsigned i = 0; i < 4096; ++i) { auto d = sphere({double(i),0,0}); if (i == 0) d.velocity = {1,0,0}; w.create(d); }
    w.step(1.0/120); EXPECT_EQ(w.statistics().islands,1u); EXPECT_GT(w.statistics().colors,1u);
    unsigned busy = 0; for (const auto& worker : jobs.statistics().workers) if (worker.tasks) ++busy; EXPECT_GE(busy,2u);
  }
}

TEST(WorldPhysics, ContinuousMultipleImpactsAndBudgetFailurePreservePublishedState) {
  for (const unsigned budget : {1u,16u}) {
    Config c; c.sleeping = false; c.maxToiEvents = budget; Physics::World w(c); w.setFields({});
    BodyDescription wall; wall.motion = MotionType::Static; wall.shape = Collider{Box{{.01f,4,4}}}; wall.restitution = 1;
    wall.position = {-2,0,0}; w.create(wall); wall.position = {2,0,0}; w.create(wall);
    auto ball = sphere(); ball.velocity = {1000,0,0}; ball.collision = CollisionMode::Continuous; ball.restitution = 1;
    const auto id = w.create(ball); w.addForce(id,{1,0,0});
    if (budget == 1) {
      EXPECT_THROW(w.step(.01),std::runtime_error);
      EXPECT_EQ(w.state(id).position,Vector{}); EXPECT_EQ(w.state(id).velocity,ball.velocity);
      EXPECT_EQ(w.time(),0); EXPECT_EQ(w.statistics().steps,0u); EXPECT_TRUE(w.contacts().empty());
      c.maxToiEvents = 16; w.configure(c);
    }
    ASSERT_NO_THROW(w.step(.01)); EXPECT_GT(w.statistics().toiEvents,1u);
    EXPECT_LT(std::abs(w.state(id).position.x),1.5); EXPECT_NEAR(std::abs(w.state(id).velocity.x),1000.01,1e-4);
  }
}

TEST(WorldPhysics, ContinuousRotatingBoxFindsImpactWithoutTranslation) {
  Config c; c.sleeping = false; Physics::World w(c); w.setFields({});
  auto obstacle = sphere({0,1.6,0}); obstacle.motion = MotionType::Static; obstacle.shape = Collider{Sphere{.2f}}; w.create(obstacle);
  BodyDescription rod; rod.shape = Collider{Box{{2,.05f,.05f}}}; rod.angularVelocity = {0,0,40}; rod.collision = CollisionMode::Continuous;
  const auto id = w.create(rod);
  ASSERT_NO_THROW(w.step(.04)); EXPECT_GT(w.statistics().toiEvents,0u);
  EXPECT_TRUE(std::isfinite(w.state(id).position.x)); EXPECT_LT(norm(w.state(id).angularVelocity),40.01);
}

TEST(WorldPhysics, FieldGenerationAndNativeSourceBindingsCannotAlias) {
  Physics::World w; w.setFields({}); auto source = w.create(sphere({4,0,0})); const auto target = w.create(sphere());
  const auto expired = w.addField(std::make_shared<UniformGravity>());
  w.setFields({}); const auto replacement = w.addField(std::make_shared<UniformGravity>());
  EXPECT_EQ(expired.slot,replacement.slot); EXPECT_NE(expired.generation,replacement.generation); EXPECT_THROW(w.removeField(expired),std::invalid_argument);
  w.setFields({std::make_shared<BodyGravity>(std::vector<BodyID>{source},1,.1)}); w.step(.01);
  EXPECT_GT(w.state(target).velocity.x,0);
  w.destroy(source); const auto recycled = w.create(sphere({-4,0,0})); EXPECT_EQ(recycled.slot,source.slot);
  const auto before = w.state(target); const auto time = w.time();
  EXPECT_THROW(w.step(.01),std::invalid_argument); EXPECT_EQ(w.time(),time); EXPECT_EQ(w.state(target).position,before.position);
}

TEST(WorldPhysics, ScalarAndSimdPreserveTheSameContactSolution) {
  std::vector<BodyState> reference;
  for (const auto backend : {SolverBackend::Scalar,SolverBackend::SSE2,SolverBackend::AVX2}) {
    Config c; c.sleeping = false; c.backend = backend; Physics::World w(c); w.setFields({});
    BodyDescription floor; floor.motion = MotionType::Static; floor.position = {0,-.5,0}; floor.shape = Collider{Box{{10,.5f,10}}}; w.create(floor);
    std::vector<BodyID> ids;
    for (unsigned i = 0; i < 16; ++i) { auto d = sphere({double(i%4),.5,double(i/4)}); d.velocity = {.7,-1,-.4}; ids.push_back(w.create(d)); }
    for (unsigned step = 0; step < 60; ++step) w.step(1.0/120);
    for (unsigned i = 0; i < ids.size(); ++i) {
      if (backend == SolverBackend::Scalar) reference.push_back(w.state(ids[i]));
      else { EXPECT_LT(norm(w.state(ids[i]).position-reference[i].position),1e-10); EXPECT_LT(norm(w.state(ids[i]).velocity-reference[i].velocity),1e-10); }
    }
  }
}

TEST(WorldPhysics, ContactWitnessesStayPreciseAtLargeWorldCoordinates) {
  std::array<BodyState,2> states;
  for (unsigned run = 0; run < states.size(); ++run) {
    const Vector origin = run ? Vector{1e9,1e9,1e9} : Vector{};
    Config config; config.sleeping = false; Physics::World world(config);
    BodyDescription floor; floor.motion = MotionType::Static; floor.position = origin+Vector{0,-.5,0}; floor.shape = Collider{Box{{10,.5f,10}}}; world.create(floor);
    BodyDescription box; box.position = origin+Vector{0,.5,0}; box.velocity = {.5,0,.2}; const auto id = world.create(box);
    for (unsigned step = 0; step < 120; ++step) world.step(1.0/120);
    states[run] = world.state(id); states[run].position = states[run].position-origin;
    EXPECT_LT(norm(states[run].angularVelocity),.001); EXPECT_NEAR(states[run].position.y,.5,.003);
  }
  EXPECT_LT(norm(states[0].position-states[1].position),1e-4);
  EXPECT_LT(norm(states[0].velocity-states[1].velocity),1e-4);
}
