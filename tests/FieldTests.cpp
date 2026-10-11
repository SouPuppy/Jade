#include "internal/Renderer/Debug/CollisionDebug.hpp"
#include <Jade/Jade.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <numbers>

namespace {
using namespace Jade;
using namespace Jade::Physics;
double length(Vec3 p) {
  return std::hypot(p.x, p.y, p.z);
}
Entity body(Scene &scene, Vec3 position, float mass = 1, float radius = 0.1f) {
  const auto e = scene.create();
  e.add<Transform>().position = position;
  e.add<Collider>(Sphere{radius});
  e.add<RigidBody>().mass = mass;
  return e;
}
void ground(Scene &scene) {
  const auto e = scene.create();
  e.add<Transform>().position = {0, -0.5f, 0};
  e.add<Collider>(Box{{4, 0.5f, 4}});
}
class InvalidForce final : public ForceField {
public:
  Vec3 sample(const FieldContext &c) const override {
    return c.body.position.x > 0.5f ? Vec3{std::numeric_limits<float>::quiet_NaN(), 0, 0} : Vec3{};
  }
};
struct Orbit {
  Scene scene;
  std::array<Entity, 3> bodies;
  Orbit() {
    constexpr double pair = 10.1, total = 110.1, outer = 8, inner = 0.9;
    const double vo = std::sqrt(total / outer), vi = std::sqrt(pair / inner);
    const std::array<double, 3> x{-pair / total * outer, 100 / total * outer - 0.1 / pair * inner,
                                  100 / total * outer + 10 / pair * inner};
    const std::array<double, 3> z{-pair / total * vo, 100 / total * vo - 0.1 / pair * vi,
                                  100 / total * vo + 10 / pair * vi};
    const std::array<float, 3> masses{100, 10, 0.1f}, radii{0.55f, 0.25f, 0.08f};
    for (std::size_t i = 0; i < 3; ++i) {
      bodies[i] = body(scene, {static_cast<float>(x[i]), 0, 0}, masses[i], radii[i]);
      bodies[i].get<RigidBody>().velocity.z = static_cast<float>(z[i]);
    }
    scene.physics().fields = {
        std::make_shared<const BodyGravity>(std::vector<Entity>{bodies.begin(), bodies.end()}, 1)};
    scene.physics().integration = Integration::VelocityVerlet;
  }
  double energy() const {
    double result = 0;
    for (const auto e : bodies) {
      const auto &b = e.get<RigidBody>();
      result += 0.5 * b.mass * length(b.velocity) * length(b.velocity);
    }
    for (std::size_t i = 0; i < bodies.size(); ++i)
      for (std::size_t j = i + 1; j < bodies.size(); ++j) {
        const double d = length(bodies[j].get<Transform>().position - bodies[i].get<Transform>().position);
        result -= static_cast<double>(bodies[i].get<RigidBody>().mass) * bodies[j].get<RigidBody>().mass /
                  std::hypot(d, 0.001);
      }
    return result;
  }
  std::array<double, 3> momentum() const {
    std::array<double, 3> result{};
    for (const auto e : bodies) {
      const auto &b = e.get<RigidBody>();
      result[0] += static_cast<double>(b.mass) * b.velocity.x;
      result[1] += static_cast<double>(b.mass) * b.velocity.y;
      result[2] += static_cast<double>(b.mass) * b.velocity.z;
    }
    return result;
  }
};
} // namespace

TEST(Fields, UniformUsesMassForForceButNotAcceleration) {
  Scene scene;
  const auto a = body(scene, {}), b = body(scene, {10, 0, 0}, 2);
  System physics;
  physics.step(scene, 1.0f / 120);
  EXPECT_FLOAT_EQ(a.get<RigidBody>().velocity.y, b.get<RigidBody>().velocity.y);
  const auto forces = physics.forceSnapshot(scene);
  ASSERT_EQ(forces.size(), 2u);
  EXPECT_TRUE(forces[0].solved);
  EXPECT_FLOAT_EQ(forces[1].fieldForce.y, 2 * forces[0].fieldForce.y);
  EXPECT_EQ(forces[0].fieldForce, forces[0].totalForce);
  EXPECT_EQ(forces[0].contactForce, Vec3{});
}
TEST(Fields, EarthMoonAndEmptyEnvironmentsAreSceneLocal) {
  Scene earth, moon, empty;
  const auto a = body(earth, {}), b = body(moon, {}), c = body(empty, {});
  moon.physics().fields = {std::make_shared<const UniformGravity>(Vec3{0, -1.62f, 0})};
  empty.physics().fields.clear();
  System physics;
  for (int i = 0; i < 120; ++i) {
    physics.step(earth, 1.0f / 120);
    physics.step(moon, 1.0f / 120);
    physics.step(empty, 1.0f / 120);
  }
  EXPECT_NEAR(a.get<RigidBody>().velocity.y, -9.81, 2e-5);
  EXPECT_NEAR(b.get<RigidBody>().velocity.y, -1.62, 2e-5);
  EXPECT_EQ(c.get<Transform>().position, Vec3{});
  EXPECT_NEAR(a.get<Transform>().position.y / b.get<Transform>().position.y, 9.81 / 1.62, 2e-5);
  physics.reset();
  EXPECT_EQ(moon.physics().fields.size(), 1u);
}
TEST(Fields, PointAndCompositeUseSoftenedAccelerationAndFiniteCenters) {
  FieldBody target{{}, {2, 0, 0}, {}, 3};
  const FieldContext c{target, {}};
  const auto point = std::make_shared<const PointGravity>(Vec3{}, 8, 1, 0.1);
  const double expected = -16 / std::pow(4.01, 1.5);
  EXPECT_NEAR(point->acceleration(c).x, expected, 1e-6);
  EXPECT_NEAR(point->sample(c).x, 3 * expected, 1e-6);
  CompositeGravity combined({point, std::make_shared<const UniformGravity>(Vec3{0, -2, 0})});
  EXPECT_EQ(combined.acceleration(c), (Vec3{point->acceleration(c).x, -2, 0}));
  target.position = {};
  EXPECT_EQ(point->sample(c), Vec3{});
  target.position = {4, 0, 0};
  const auto far = point->acceleration(c).x;
  EXPECT_NEAR(expected / far, 4, 0.02); // inverse square away from softened center
}
TEST(Fields, InvalidParametersSourcesAndNullFieldsAreRejected) {
  EXPECT_THROW(UniformGravity(Vec3{std::numeric_limits<float>::infinity(), 0, 0}), std::invalid_argument);
  EXPECT_THROW(PointGravity(Vec3{}, -1), std::invalid_argument);
  EXPECT_THROW(PointGravity(Vec3{}, 1, 1, 0), std::invalid_argument);
  EXPECT_THROW(CompositeGravity({nullptr}), std::invalid_argument);
  Scene scene, foreign;
  const auto source = body(foreign, {});
  scene.physics().fields = {std::make_shared<const BodyGravity>(std::vector<Entity>{source}, 1)};
  System physics;
  EXPECT_THROW(physics.step(scene, 0.01f), std::invalid_argument);
  EXPECT_THROW(BodyGravity(std::vector<Entity>{source, source}, 1), std::invalid_argument);
  scene.physics().fields = {nullptr};
  EXPECT_THROW(physics.step(scene, 0.01f), std::invalid_argument);
  const auto local = body(scene, {});
  local.get<RigidBody>().mass = 0;
  scene.physics().fields = {
      std::make_shared<const CompositeGravity>(std::vector<std::shared_ptr<const GravityField>>{
          std::make_shared<const BodyGravity>(std::vector<Entity>{local}, 1)})};
  EXPECT_THROW(physics.step(scene, 0.01f), std::invalid_argument);
  local.get<RigidBody>().mass = 1;
  local.remove<Collider>();
  EXPECT_THROW(physics.step(scene, 0.01f), std::invalid_argument);
  scene.physics().fields.clear();
  scene.physics().integration = static_cast<Integration>(99);
  EXPECT_THROW(physics.step(scene, 0.01f), std::invalid_argument);
}
TEST(Fields, BodySourcesExcludeSelfAndDoNotSurviveRecycling) {
  Scene scene;
  const auto a = body(scene, {-2, 0, 0}, 2), b = body(scene, {2, 0, 0}, 3);
  scene.physics().fields = {std::make_shared<const BodyGravity>(std::vector<Entity>{a, b}, 1)};
  System physics;
  const auto forces = physics.forceSnapshot(scene);
  ASSERT_EQ(forces.size(), 2u);
  EXPECT_NEAR(forces[0].fieldForce.x, -forces[1].fieldForce.x, 1e-7);
  EXPECT_GT(forces[0].fieldForce.x, 0);
  scene.destroy(b);
  const auto fresh = body(scene, {2, 0, 0}, 99);
  const auto after = physics.forceSnapshot(scene);
  ASSERT_EQ(after.size(), 2u);
  const auto found = std::find_if(after.begin(), after.end(), [&](const auto &f) { return f.entity == a; });
  ASSERT_NE(found, after.end());
  EXPECT_EQ(found->fieldForce, Vec3{});
  EXPECT_TRUE(fresh.valid());
  physics.step(scene, 0.01f);
  EXPECT_EQ(a.get<RigidBody>().velocity, Vec3{});
}
TEST(Fields, InvalidSamplingRollsBackBothFirstAndSecondVerletStages) {
  for (const auto method : {Integration::SemiImplicitEuler, Integration::VelocityVerlet}) {
    Scene scene;
    const auto a = body(scene, {}), b = body(scene, {3, 0, 0});
    System physics;
    physics.step(scene, 0.01f);
    const auto previousA = a.get<Transform>(), previousB = b.get<Transform>();
    const auto velocity = a.get<RigidBody>().velocity;
    const auto steps = physics.statistics().steps;
    scene.physics().integration = method;
    scene.physics().fields = {std::make_shared<const InvalidForce>()};
    EXPECT_THROW(physics.step(scene, 0.01f), std::invalid_argument);
    EXPECT_EQ(a.get<Transform>().position, previousA.position);
    EXPECT_EQ(b.get<Transform>().position, previousB.position);
    EXPECT_EQ(a.get<RigidBody>().velocity, velocity);
    EXPECT_EQ(physics.statistics().steps, steps);
  }
  Scene scene;
  const auto e = body(scene, {});
  e.get<RigidBody>().velocity.x = 1;
  scene.physics().integration = Integration::VelocityVerlet;
  scene.physics().fields = {std::make_shared<const InvalidForce>()};
  System physics;
  EXPECT_THROW(physics.step(scene, 1), std::invalid_argument);
  EXPECT_EQ(e.get<Transform>().position, Vec3{});
  EXPECT_FLOAT_EQ(e.get<RigidBody>().velocity.x, 1);
  EXPECT_EQ(physics.statistics().steps, 0u);
}
TEST(Fields, SupportFrictionAndWarmStartingProduceNetAppliedForce) {
  Scene scene;
  ground(scene);
  const auto e = body(scene, {0, 0.5f, 0});
  e.get<Collider>() = Collider{Box{}};
  System physics;
  for (int i = 0; i < 300; ++i)
    physics.step(scene, 1.0f / 120);
  auto forces = physics.forceSnapshot(scene);
  ASSERT_EQ(forces.size(), 1u);
  EXPECT_NEAR(forces[0].fieldForce.y, -9.81, 1e-5);
  EXPECT_NEAR(forces[0].contactForce.y, 9.81, 1e-4);
  EXPECT_LT(length(forces[0].totalForce), 1e-4);
  for (int i = 0; i < 10; ++i) {
    physics.step(scene, 1.0f / 120);
    EXPECT_LT(length(physics.forceSnapshot(scene)[0].totalForce), 1e-4);
  }
  e.get<RigidBody>().velocity.x = 2;
  const Vec3 before = e.get<RigidBody>().velocity;
  physics.step(scene, 1.0f / 120);
  forces = physics.forceSnapshot(scene);
  EXPECT_LT(forces[0].contactForce.x, 0);
  EXPECT_NEAR(forces[0].totalForce.x, (e.get<RigidBody>().velocity.x - before.x) * 120, 1e-4);
}
TEST(Fields, ForceSnapshotsInvalidateOnEditsImpulsesFieldsAndReset) {
  Scene scene;
  const auto e = body(scene, {});
  System physics;
  EXPECT_FALSE(physics.forceSnapshot(scene)[0].solved);
  physics.step(scene, 0.01f);
  EXPECT_TRUE(physics.forceSnapshot(scene)[0].solved);
  e.get<Transform>().position.x = 1;
  EXPECT_FALSE(physics.forceSnapshot(scene)[0].solved);
  physics.step(scene, 0.01f);
  physics.applyImpulse(scene, e, {1, 0, 0}, e.get<Transform>().position);
  EXPECT_FALSE(physics.forceSnapshot(scene)[0].solved);
  physics.step(scene, 0.01f);
  scene.physics().fields.clear();
  const auto fresh = physics.forceSnapshot(scene);
  EXPECT_FALSE(fresh[0].solved);
  EXPECT_EQ(fresh[0].fieldForce, Vec3{});
  physics.step(scene, 0.01f);
  EXPECT_TRUE(physics.forceSnapshot(scene)[0].solved);
  physics.reset();
  EXPECT_FALSE(physics.forceSnapshot(scene)[0].solved);
  scene.destroy(e);
  EXPECT_TRUE(physics.forceSnapshot(scene).empty());
}
TEST(Fields, VerletReportsTheAverageForceActuallyAppliedAcrossBothStages) {
  Scene scene;
  const auto e = body(scene, {2, 0, 0}, 2);
  e.get<RigidBody>().velocity.z = 1;
  const auto before = e.get<RigidBody>().velocity;
  scene.physics().fields = {std::make_shared<const PointGravity>(Vec3{}, 8, 1, 0.1)};
  scene.physics().integration = Integration::VelocityVerlet;
  System physics;
  physics.step(scene, 0.2f);
  const auto forces = physics.forceSnapshot(scene);
  ASSERT_EQ(forces.size(), 1u);
  const auto applied = (e.get<RigidBody>().velocity - before) * 10;
  EXPECT_NEAR(forces[0].fieldForce.x, applied.x, 1e-5);
  EXPECT_NEAR(forces[0].fieldForce.z, applied.z, 1e-5);
  EXPECT_LT(forces[0].fieldForce.z, 0);
  EXPECT_EQ(forces[0].fieldForce, forces[0].totalForce);
}

TEST(Fields, TwoBodyVelocityVerletHasSecondOrderConvergence) {
  const auto error = [](int steps) {
    Scene scene;
    const auto e = body(scene, {1, 0, 0});
    const double omega = std::pow(1.000001, -0.75);
    e.get<RigidBody>().velocity.z = static_cast<float>(omega);
    scene.physics().fields = {std::make_shared<const PointGravity>(Vec3{}, 1, 1)};
    scene.physics().integration = Integration::VelocityVerlet;
    System physics;
    const float dt = 4.0f / steps;
    for (int i = 0; i < steps; ++i)
      physics.step(scene, dt);
    return length(e.get<Transform>().position -
                  Vec3{static_cast<float>(std::cos(4 * omega)), 0, static_cast<float>(std::sin(4 * omega))});
  };
  const double ratio = error(40) / error(80);
  EXPECT_GT(ratio, 3.8);
  EXPECT_LT(ratio, 4.2);
}
TEST(Fields, ThreeBodyOrbitPreservesEnergyMomentumAndMoonOverOneHundredOuterPeriods) {
  Orbit orbit;
  System physics;
  const double initialEnergy = orbit.energy();
  const auto initialMomentum = orbit.momentum();
  double scale = 0;
  for (const auto e : orbit.bodies)
    scale += e.get<RigidBody>().mass * length(e.get<RigidBody>().velocity);
  constexpr float dt = 1.0f / 120;
  const int steps = static_cast<int>(std::ceil(100 * 2 * std::numbers::pi * std::sqrt(512 / 110.1) / dt));
  double energyError = 0, momentumError = 0;
  for (int step = 0; step < steps; ++step) {
    physics.step(orbit.scene, dt);
    if (step % 120 == 0) {
      energyError = std::max(energyError, std::abs((orbit.energy() - initialEnergy) / initialEnergy));
      const auto p = orbit.momentum();
      momentumError = std::max(momentumError, std::hypot(p[0] - initialMomentum[0], p[1] - initialMomentum[1],
                                                         p[2] - initialMomentum[2]) /
                                                  scale);
      const double moonRadius =
          length(orbit.bodies[2].get<Transform>().position - orbit.bodies[1].get<Transform>().position);
      ASSERT_GT(moonRadius, 0.5);
      ASSERT_LT(moonRadius, 1.4);
    }
  }
  EXPECT_LT(energyError, 1e-3);
  EXPECT_LT(momentumError, 1e-4);
  RecordProperty("maximum_relative_energy_error", std::to_string(energyError));
  RecordProperty("maximum_normalized_momentum_drift", std::to_string(momentumError));
}
TEST(Fields, WorkerIntegrationMatchesSerialForMovingSources) {
  Orbit serial, parallel;
  JobSystem jobs({3});
  System a, b(jobs);
  for (int step = 0; step < 120; ++step) {
    a.step(serial.scene, 1.0f / 120);
    b.step(parallel.scene, 1.0f / 120);
    for (std::size_t i = 0; i < 3; ++i) {
      EXPECT_EQ(serial.bodies[i].get<Transform>().position, parallel.bodies[i].get<Transform>().position);
      EXPECT_EQ(serial.bodies[i].get<RigidBody>().velocity, parallel.bodies[i].get<RigidBody>().velocity);
    }
  }
}
TEST(Fields, DebugArrowsShowFieldAndSolvedTotalAndHideBalancedSupport) {
  Scene scene;
  const auto e = body(scene, {0, 2, 0});
  System physics;
  auto debug = Detail::captureCollisionDebug(scene, false, false, false, &physics, true);
  EXPECT_EQ(debug.fieldForces.size(), 6u);
  EXPECT_TRUE(debug.totalForces.empty());
  physics.step(scene, 0.01f);
  debug = Detail::captureCollisionDebug(scene, false, false, false, &physics, true);
  ASSERT_EQ(debug.fieldForces.size(), 6u);
  ASSERT_EQ(debug.totalForces.size(), 6u);
  EXPECT_NEAR(length(debug.fieldForces[1] - debug.fieldForces[0]), 0.8, 1e-6);
  EXPECT_NEAR(length(debug.totalForces[1] - debug.totalForces[0]), 0.6, 1e-6);
  ground(scene);
  e.get<Collider>() = Collider{Box{}};
  e.get<Transform>().position.y = 0.5f;
  e.get<RigidBody>().velocity = {};
  for (int i = 0; i < 300; ++i)
    physics.step(scene, 1.0f / 120);
  debug = Detail::captureCollisionDebug(scene, false, false, false, &physics, true);
  EXPECT_EQ(debug.fieldForces.size(), 6u);
  EXPECT_TRUE(debug.totalForces.empty());
}
