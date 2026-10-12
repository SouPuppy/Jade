#include <Jade/Core/Scene.hpp>
#include <Jade/Core/Transform.hpp>
#include <Jade/Assets/Assets.hpp>
#include <Jade/Graphics/Geometry.hpp>
#include <Jade/Materials/Material.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
struct Counter {
  int value = 0;
};
struct Label {
  int value = 0;
};
struct Owned {
  std::unique_ptr<int> value;
  explicit Owned(int initial) : value(std::make_unique<int>(initial)) {}
};
struct OnDestroy {
  std::function<void()> callback;
  explicit OnDestroy(std::function<void()> fn) : callback(std::move(fn)) {}
  ~OnDestroy() { callback(); }
};
struct Throws {
  Throws() { throw std::runtime_error("constructor"); }
};
} // namespace

TEST(Scene, EmptyIdentitiesAcceptPlainAndNoncopyableComponents)
{
  Jade::Scene scene{std::make_shared<Jade::World>()};
  auto entity = scene.world().create();
  EXPECT_TRUE(entity.valid());
  EXPECT_TRUE(scene.world().owns(entity));
  EXPECT_FALSE(entity.has<Jade::Transform>());
  EXPECT_EQ(entity.add<Counter>(42).value, 42);
  EXPECT_EQ(*entity.add<Owned>(7).value, 7);
  const auto handle = entity;
  handle.get<Counter>().value = 43;
  EXPECT_EQ(entity.get<Counter>().value, 43);
  EXPECT_THROW(entity.add<Counter>(), std::logic_error);
  EXPECT_THROW(entity.get<Label>(), std::out_of_range);
  EXPECT_NO_THROW(entity.remove<Label>());
  entity.remove<Counter>();
  EXPECT_FALSE(entity.has<Counter>());
  EXPECT_EQ(entity.add<Counter>().value, 0);
  EXPECT_THROW(entity.add<Throws>(), std::runtime_error);
  EXPECT_FALSE(entity.has<Throws>());
}

TEST(Scene, DestroyedEntitiesNeverAliasReusedSlotsAndOwnersStayDistinct)
{
  Jade::Scene scene{std::make_shared<Jade::World>()}; Jade::Scene other{std::make_shared<Jade::World>()};
  const auto old = scene.world().create();
  const auto foreign = other.world().create();
  EXPECT_EQ(old.id(), foreign.id());
  EXPECT_NE(old, foreign);
  EXPECT_FALSE(scene.world().owns(foreign));
  EXPECT_THROW(scene.world().destroy(foreign), std::invalid_argument);
  old.add<Counter>(5);
  scene.world().destroy(old);
  const auto replacement = scene.world().create();
  replacement.add<Counter>(99);
  EXPECT_NE(old.id(), replacement.id());
  EXPECT_FALSE(old.valid());
  EXPECT_FALSE(old.has<Counter>());
  EXPECT_THROW(old.get<Counter>(), std::logic_error);
  EXPECT_THROW(old.add<Counter>(), std::logic_error);
  EXPECT_THROW(old.remove<Counter>(), std::logic_error);
  EXPECT_THROW(scene.world().destroy(old), std::logic_error);
  EXPECT_EQ(replacement.get<Counter>().value, 99);
}

TEST(Scene, HandlesExpireWithSceneAndComponentsAreDestroyedExactlyOnce)
{
  Jade::Entity handle;
  int destructions = 0;
  {
    Jade::Scene scene{std::make_shared<Jade::World>()};
    handle = scene.world().create();
    handle.add<OnDestroy>([&] { ++destructions; });
    auto removed = scene.world().create();
    removed.add<OnDestroy>([&] { ++destructions; });
    removed.remove<OnDestroy>();
    EXPECT_EQ(destructions, 1);
    removed.remove<OnDestroy>();
    removed.add<OnDestroy>([&] { ++destructions; });
    scene.world().destroy(removed);
    EXPECT_EQ(destructions, 2);
  }
  EXPECT_EQ(destructions, 3);
  EXPECT_FALSE(handle.valid());
  EXPECT_FALSE(handle.has<OnDestroy>());
  EXPECT_THROW(handle.add<Counter>(), std::logic_error);
  EXPECT_THROW(handle.get<OnDestroy>(), std::logic_error);
}

TEST(Scene, ComponentReferencesRemainStableAcrossPoolAndEntityGrowth)
{
  Jade::Scene scene{std::make_shared<Jade::World>()};
  auto first = scene.world().create();
  auto &counter = first.add<Counter>(9);
  const auto *address = &counter;
  for (int index = 0; index < 1024; ++index) {
    auto next = scene.world().create();
    next.add<Counter>(index);
    next.add<Label>(index);
  }
  EXPECT_EQ(&first.get<Counter>(), address);
  EXPECT_EQ(counter.value, 9);
}

TEST(Scene, QueriesSnapshotMatchesSkipRemovalsAndDeferNewMatches)
{
  Jade::Scene scene{std::make_shared<Jade::World>()};
  const auto first = scene.world().create(), erased = scene.world().create(), stripped = scene.world().create();
  const auto missing = scene.world().create();
  for (auto entity : {first, erased, stripped}) {
    entity.add<Counter>();
    entity.add<Label>();
  }
  missing.add<Counter>();
  Jade::Entity created;
  std::vector<Jade::Entity> visited;
  for (const auto entity : scene.world().registry().query<Counter, Label>()) {
    visited.push_back(entity);
    if (entity == first) {
      scene.world().destroy(erased);
      stripped.remove<Label>();
      missing.add<Label>();
      created = scene.world().create(); // May reuse erased's slot, with a different generation.
      created.add<Counter>();
      created.add<Label>();
    }
  }
  EXPECT_EQ(visited, (std::vector<Jade::Entity>{first}));
  visited.clear();
  for (const auto entity : scene.world().registry().query<Counter, Label>())
    visited.push_back(entity);
  ASSERT_EQ(visited.size(), 3u);
  EXPECT_NE(std::find(visited.begin(), visited.end(), created), visited.end());
  EXPECT_NE(std::find(visited.begin(), visited.end(), missing), visited.end());
}

TEST(Scene, ComponentDestructorsCanMutateTheSceneAfterRemoval)
{
  Jade::Scene scene{std::make_shared<Jade::World>()};
  const auto entity = scene.world().create();
  entity.add<OnDestroy>([&] {
    EXPECT_FALSE(entity.has<OnDestroy>());
    entity.add<Counter>(17);
    scene.world().create().add<Label>();
  });
  entity.remove<OnDestroy>();
  EXPECT_EQ(entity.get<Counter>().value, 17);
  entity.add<OnDestroy>([&] {
    EXPECT_FALSE(entity.valid());
    scene.world().create().add<Counter>(88);
  });
  scene.world().destroy(entity);
  EXPECT_FALSE(entity.valid());
}

TEST(Scene, ConstructorDestroyingItsEntityCannotAttachToAReusedIdentity)
{
  struct DuringConstruction {
    explicit DuringConstruction(std::function<void()> callback) { callback(); }
  };
  Jade::Scene scene{std::make_shared<Jade::World>()};
  auto entity = scene.world().create();
  Jade::Entity next;
  EXPECT_THROW(entity.add<DuringConstruction>([&] {
    scene.world().destroy(entity);
    next = scene.world().create();
  }),
               std::logic_error);
  EXPECT_TRUE(next.valid());
  EXPECT_FALSE(next.has<DuringConstruction>());
}

TEST(Assets, SharedImmutableMeshesKeepIndependentEntityState)
{
  Jade::Assets assets;
  Jade::Scene scene{std::make_shared<Jade::World>()};
  const auto mesh = assets.add(Jade::MeshData::cube());
  const auto first = scene.world().create(), second = scene.world().create();
  first.add<Jade::Transform>().position = {-2, 0, 0};
  second.add<Jade::Transform>().position = {2, 0, 0};
  first.add<Jade::Geometry>(mesh);
  first.add<Jade::Material>().baseColor = {1, 0, 0};
  second.add<Jade::Geometry>(mesh);
  second.add<Jade::Material>().baseColor = {0, 0, 1};
  static_assert(std::is_same_v<decltype(assets.get(mesh)), const Jade::Mesh &>);
  EXPECT_EQ(&assets.get(first.get<Jade::Geometry>().resource), &assets.get(second.get<Jade::Geometry>().resource));
  first.get<Jade::Transform>().position.x = -3;
  EXPECT_FLOAT_EQ(second.get<Jade::Transform>().position.x, 2);
  EXPECT_FLOAT_EQ(second.get<Jade::Material>().baseColor.b, 1);
  const auto *address = &assets.get(mesh);
  for (int index = 0; index < 128; ++index)
    assets.add(Counter{index});
  EXPECT_EQ(&assets.get(mesh), address);
  scene.world().destroy(first);
  EXPECT_TRUE(mesh.valid());
  EXPECT_EQ(assets.get(mesh).data().indices.size(), 36u);
}

TEST(Assets, RejectsForeignEmptyAndExpiredHandles)
{
  Jade::Assets assets;
  Jade::MeshHandle expired;
  EXPECT_TRUE(expired.empty());
  EXPECT_THROW(assets.get(expired), std::invalid_argument);
  {
    Jade::Assets other;
    expired = other.add(Jade::MeshData::cube());
    EXPECT_THROW(assets.get(expired), std::invalid_argument);
    EXPECT_TRUE(expired.valid());
  }
  EXPECT_FALSE(expired.valid());
  EXPECT_FALSE(expired.empty());
  EXPECT_THROW(assets.get(expired), std::invalid_argument);
  const auto data = assets.add(Counter{5});
  EXPECT_EQ(assets.get(data).value, 5);
}

TEST(Assets, CanonicalObjPathsReturnTheSameResource)
{
  Jade::Assets assets;
  const auto directory = std::filesystem::path(JADE_TEST_ASSET_DIR);
  const auto first = assets.load<Jade::Mesh>(directory / "bunny.obj");
  const auto alias = assets.load<Jade::Mesh>(directory / "." / "bunny.obj");
  EXPECT_EQ(first, alias);
  EXPECT_EQ(&assets.get(first), &assets.get(alias));
  EXPECT_THROW(assets.load<Jade::Mesh>(directory / "missing.obj"), std::runtime_error);
  EXPECT_EQ(assets.load<Jade::Mesh>(directory / "bunny.obj"), first);
}
