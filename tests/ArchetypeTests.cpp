#include <Jade/Jade.hpp>
#include "internal/Core/Registry.hpp"
#include <gtest/gtest.h>
#include <type_traits>

namespace {
struct A { int value = 7; };
struct B {};
struct C {};
using Object = Jade::Archetype<Jade::Geometry, Jade::Transform, Jade::Material>;
using Physical = Jade::Archetype<Jade::Transform, Jade::Collider, Jade::RigidBody>;
class Ball : public Jade::Archetype<Object, Physical> {
  using Base = Jade::Archetype<Object, Physical>;
public:
  explicit Ball(Jade::Entity entity) : Base(entity) { add<Jade::Collider>(Jade::Sphere{0.5f}); }
};
struct Missing : Physical { using Physical::Physical; };
struct Throws : Jade::Archetype<A> {
  explicit Throws(Jade::Entity e) : Jade::Archetype<A>(e) { throw std::runtime_error("constructor"); }
};
struct Configured : Jade::Archetype<A, B> {
  Configured(Jade::Entity e, int value, std::unique_ptr<int> payload) : Jade::Archetype<A, B>(e) {
    get<A>().value = value + *payload;
    if (value < 0) throw std::runtime_error("configuration");
  }
};
}
static_assert(std::is_same_v<Jade::Archetype<Jade::Archetype<A, B>, B, C>, Jade::Archetype<A, B, C>>);
TEST(Archetype, ComposesTypedHandlesAndQueriesActualComponents) {
  Jade::Scene scene;
  const auto ball = scene.create<Ball>();
  EXPECT_TRUE(ball.has<Jade::Geometry>());
  EXPECT_TRUE(ball.has<Jade::Transform>());
  EXPECT_TRUE(ball.has<Jade::Collider>());
  EXPECT_TRUE(ball.has<Jade::RigidBody>());
  EXPECT_FALSE(ball.has<Ball>());
  const auto copy = ball;
  EXPECT_EQ(copy, ball);
  copy.get<Jade::RigidBody>().mass = 2;
  EXPECT_EQ(ball.get<Jade::RigidBody>().mass, 2);
  const auto manual = scene.create();
  manual.add<Jade::Transform>(); manual.add<Jade::Collider>(Jade::Box{}); manual.add<Jade::RigidBody>();
  auto query = scene.query<Physical>();
  EXPECT_EQ(std::distance(query.begin(), query.end()), 2);
  manual.remove<Jade::RigidBody>();
  EXPECT_EQ(std::distance(query.begin(), query.end()), 1);
  scene.destroy(ball);
  EXPECT_FALSE(copy);
  const auto identities = scene.query<Jade::Archetype<>>();
  ASSERT_EQ(std::distance(identities.begin(), identities.end()), 1);
  scene.destroy(manual);
  EXPECT_TRUE(scene.create<Jade::Archetype<>>());
  EXPECT_EQ(identities.begin(), identities.end());
}
TEST(Archetype, FailedCreationLeavesNoPartialEntities) {
  Jade::Scene scene;
  EXPECT_THROW(scene.create<Missing>(), std::logic_error);
  EXPECT_THROW(scene.create<Throws>(), std::runtime_error);
  const auto physical = scene.query<Jade::Transform>();
  EXPECT_EQ(physical.begin(), physical.end());
  const auto values = scene.query<A>();
  EXPECT_EQ(values.begin(), values.end());
  EXPECT_TRUE(scene.create<Jade::Archetype<A>>().has<A>());
}
TEST(Archetype, ForwardsConstructorArgumentsAndRollsBackConfigurationFailures) {
  Jade::Scene scene;
  const auto object = scene.create<Configured>(4, std::make_unique<int>(8));
  EXPECT_EQ(object.get<A>().value, 12);
  EXPECT_THROW(scene.create<Configured>(-1, std::make_unique<int>(8)), std::runtime_error);
  const auto objects = scene.query<A, B>();
  EXPECT_EQ(std::distance(objects.begin(), objects.end()), 1);
}
TEST(Archetype, StructuralChangesKeepStableComponentObjects) {
  Jade::Scene scene;
  const auto e = scene.create<Jade::Archetype<A>>();
  auto* original = &e.get<A>();
  e.add<B>(); e.add<C>(); e.remove<B>();
  for (int i = 0; i < 100; ++i) scene.create<Jade::Archetype<A, B>>();
  EXPECT_EQ(&e.get<A>(), original);
  EXPECT_EQ(original->value, 7);
}
TEST(Archetype, SignatureQueriesVisitMatchingRowsOnlyAndTrackMigrations) {
  Jade::Detail::Registry registry;
  for (int i = 0; i < 5000; ++i) {
    const auto [index, generation] = registry.Create();
    (void)generation;
    registry.Add(index, typeid(A), std::make_shared<A>());
  }
  const auto [index, generation] = registry.Create();
  registry.Add(index, typeid(B), std::make_shared<B>());
  EXPECT_EQ(registry.Matching({typeid(B)}).size(), 1u);
  EXPECT_EQ(registry.LastQueryRows(), 1u);
  registry.Add(index, typeid(C), std::make_shared<C>());
  EXPECT_EQ(registry.Matching({typeid(B)}).size(), 1u);
  EXPECT_EQ(registry.Matching({typeid(B), typeid(C)}).size(), 1u);
  registry.Remove(index, typeid(B));
  EXPECT_TRUE(registry.Matching({typeid(B)}).empty());
  EXPECT_EQ(registry.LastQueryRows(), 0u);
  registry.Destroy(index, generation);
  EXPECT_TRUE(registry.Matching({typeid(C)}).empty());
}
