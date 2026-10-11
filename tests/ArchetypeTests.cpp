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
struct Ball {
  using Components = Jade::Archetype<Object, Physical>;
  Ball() = delete;
};
struct NamedAB { using Components = Jade::Archetype<A, B>; };
struct NamedAgain { using Components = NamedAB; };
struct Throws {
  Throws() { throw std::runtime_error("component constructor"); }
};
struct Tracked {
  inline static int live = 0;
  inline static int constructions = 0;
  Tracked() { ++live; ++constructions; }
  ~Tracked() { --live; }
  Tracked(const Tracked&) = delete;
  Tracked& operator=(const Tracked&) = delete;
};
struct RemovesPrevious {
  inline static Jade::Scene* scene = nullptr;
  RemovesPrevious() {
    for (const auto entity : scene->query<Tracked>()) entity.remove<Tracked>();
  }
};
}
static_assert(std::is_same_v<Jade::Archetype<Jade::Archetype<A, B>, B, C>, Jade::Archetype<A, B, C>>);
static_assert(std::is_same_v<Jade::Archetype<NamedAgain, B, C>, Jade::Archetype<A, B, C>>);
static_assert(std::is_empty_v<Object>);
static_assert(!std::is_base_of_v<Jade::Entity, Object>);
static_assert(std::is_same_v<decltype(std::declval<Jade::Scene&>().create<Ball>()), Jade::Entity>);
TEST(Archetype, DescriptorsCreateEntitiesAndQueriesMatchActualComponents) {
  Jade::Scene scene;
  const auto ball = scene.create<Ball>();
  EXPECT_TRUE(ball.has<Jade::Geometry>());
  EXPECT_TRUE(ball.has<Jade::Transform>());
  EXPECT_TRUE(ball.has<Jade::Collider>());
  EXPECT_TRUE(ball.has<Jade::RigidBody>());
  EXPECT_TRUE(ball.get<Jade::Geometry>().mesh.empty());
  EXPECT_TRUE(ball.get<Jade::Collider>().empty());
  EXPECT_EQ(ball.get<Jade::Transform>().position, (Jade::Vec3{}));
  EXPECT_EQ(ball.get<Jade::Transform>().scale, (Jade::Vec3{1, 1, 1}));
  EXPECT_FLOAT_EQ(ball.get<Jade::RigidBody>().mass, 1);
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
  manual.add<Jade::Geometry>(); manual.add<Jade::Material>(); manual.add<Jade::RigidBody>();
  const auto named = scene.query<Ball>();
  EXPECT_EQ(std::distance(named.begin(), named.end()), 2);
  ball.get<Jade::Collider>() = Jade::Collider{Jade::Sphere{0.5f}};
  ball.get<Jade::Collider>() = {};
  EXPECT_EQ(std::distance(named.begin(), named.end()), 2);
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
  const auto existing = scene.create<Jade::Archetype<A>>();
  auto* value = &existing.get<A>();
  Tracked::constructions = 0;
  EXPECT_THROW((scene.create<Jade::Archetype<A, Tracked, Throws, B>>()), std::runtime_error);
  EXPECT_EQ(Tracked::constructions, 1);
  EXPECT_EQ(Tracked::live, 0);
  const auto identities = scene.query<Jade::Archetype<>>();
  EXPECT_EQ(std::distance(identities.begin(), identities.end()), 1);
  const auto values = scene.query<A>();
  EXPECT_EQ(std::distance(values.begin(), values.end()), 1);
  EXPECT_EQ(&existing.get<A>(), value);
  EXPECT_TRUE(scene.create<Jade::Archetype<A>>().has<A>());
}
TEST(Archetype, ExplicitComponentsDeduplicateAndAreConfiguredAfterCreation) {
  Jade::Scene scene;
  Tracked::constructions = 0;
  using Composite = Jade::Archetype<NamedAgain, Tracked, Jade::Archetype<Tracked, C>, A>;
  const auto object = scene.create<Composite>();
  EXPECT_EQ(Tracked::constructions, 1);
  EXPECT_EQ(Tracked::live, 1);
  EXPECT_FALSE(object.has<Jade::Transform>());
  EXPECT_EQ(object.get<A>().value, 7);
  object.get<A>().value = 12;
  EXPECT_EQ(object.get<A>().value, 12);
  const auto objects = scene.query<NamedAgain, C>();
  EXPECT_EQ(std::distance(objects.begin(), objects.end()), 1);
  const auto empty = scene.create<Jade::Archetype<>>();
  EXPECT_FALSE(empty.has<Jade::Transform>());
  scene.destroy(object);
  EXPECT_EQ(Tracked::live, 0);
}
TEST(Archetype, ComponentConstructorStructuralEditsCannotPublishAnIncompleteEntity) {
  Jade::Scene scene;
  RemovesPrevious::scene = &scene;
  EXPECT_THROW((scene.create<Jade::Archetype<Tracked, RemovesPrevious>>()), std::logic_error);
  RemovesPrevious::scene = nullptr;
  EXPECT_EQ(Tracked::live, 0);
  const auto all = scene.query<Jade::Archetype<>>();
  EXPECT_EQ(all.begin(), all.end());
  const auto value = scene.create<Jade::Archetype<A>>();
  EXPECT_EQ(value.get<A>().value, 7);
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
