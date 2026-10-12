#include <Jade/Jade.hpp>
#include <gtest/gtest.h>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>

namespace {
using namespace Jade;
struct Provider : ChunkProvider {
  bool dynamic = true;
  bool contains(ChunkCoord c) const noexcept override { return c.x >= -16 && c.x < 16 && c.z >= -16 && c.z < 16; }
  ChunkData load(ChunkCoord c) const override {
    return {c, {{static_cast<ObjectId>(1+(c.z+16)*32+c.x+16),"test",std::to_string(c.x*16+8),true,dynamic}}};
  }
};
struct Store : WorldStore {
  WorldState data;
  bool fail = false;
  WorldState load(const std::string& id) const override { auto state=data;state.world=id;return state; }
  void save(const WorldState& s) override { if(fail)throw std::runtime_error("save failure");data=s; }
};
ObjectCodec codec() {
  return {
    [](Jade::World&,Entity e,const ObjectData& d) {
      e.add<Transform>().position={std::stof(d.state),2,static_cast<float>((static_cast<int>(d.id-1)/32-16)*16+8)};
      e.add<Collider>(Sphere{1}); e.add<RigidBody>();
    },
    [](const Jade::World&,Entity e) { return std::to_string(e.get<Transform>().position.x); }
  };
}
ObjectRef reference(ChunkCoord c) {return {"test-world",static_cast<ObjectId>(1+(c.z+16)*32+c.x+16)};}
void configure(Jade::World& w,std::shared_ptr<Store> store={}) {
  StreamingSettings settings;settings.world="test-world";settings.releaseDelay=0;
  w.streaming().configure(settings,std::make_shared<Provider>(),{{"test",codec()}},store);
}
struct Value { int value = 0; };
struct GatedProvider : Provider {
  mutable std::mutex mutex;
  mutable std::condition_variable changed;
  mutable unsigned started = 0;
  bool open = false;
  ChunkData load(ChunkCoord coord) const override {
    { std::unique_lock lock(mutex); ++started; changed.notify_all(); changed.wait(lock,[&]{return open;}); }
    return Provider::load(coord);
  }
  void release() { std::lock_guard lock(mutex);open=true;changed.notify_all(); }
};
struct GatedStore : Store {
  std::mutex mutex;std::condition_variable changed;bool started=false,open=false;
  void save(const WorldState& state) override {
    {std::unique_lock lock(mutex);started=true;changed.notify_all();changed.wait(lock,[&]{return open;});}
    Store::save(state);
  }
  void release() {std::lock_guard lock(mutex);open=true;changed.notify_all();}
};
}

TEST(WorldRuntime, PagedStorageStableReferencesAndTraversalCommands) {
  Jade::World world;
  auto first=world.create<Archetype<Value>>();auto* address=&first.get<Value>();
  for(int i=0;i<4096;++i)world.create<Archetype<Value>>();
  EXPECT_EQ(address,&first.get<Value>());
  unsigned visited=0;
  world.registry().each<Value>([&](Entity e,Value& value) {
    ++visited;++value.value;
    if(e==first){EXPECT_THROW(world.destroy(e),std::logic_error);world.command([e](Jade::World& w){w.destroy(e);});}
  });
  EXPECT_EQ(visited,4097u);EXPECT_TRUE(first);world.commit();EXPECT_FALSE(first);
}
TEST(WorldRuntime, ViewsShareWorldAndFixedSimulationRunsWithoutScene) {
  auto world=std::make_shared<Jade::World>();Scene a(world),b(world);
  auto e=world->create<Archetype<Transform,Collider,RigidBody>>();e.get<Collider>()=Collider{Sphere{1}};
  Physics::System physics;
  world->tick(physics,1.0/120);const auto y=e.get<Transform>().position.y;
  EXPECT_LT(y,0);EXPECT_EQ(&a.world(),&b.world());
  world->tick(physics,1.0/120);EXPECT_LT(e.get<Transform>().position.y,y);
}
TEST(WorldRuntime, NegativeCoordinatesAndRadiusPolicies) {
  EXPECT_EQ(chunkAt({-.01f,0,-16}), (ChunkCoord{-1,-1}));
  EXPECT_EQ(chunkAt({16,0,0}), (ChunkCoord{1,0}));
  EXPECT_THROW(chunkAt({},0),std::invalid_argument);
  Jade::World world;configure(world);world.streaming().addSource({{8,0,8}});world.streaming().barrier();
  auto stats=world.streaming().statistics();EXPECT_EQ(stats.data,25u);EXPECT_EQ(stats.rendered,25u);EXPECT_EQ(stats.active,9u);
  auto center=world.streaming().resolve(reference({0,0}));auto outer=world.streaming().resolve(reference({2,0}));
  ASSERT_TRUE(center);ASSERT_TRUE(outer);EXPECT_TRUE(center.get<Residency>().collision);EXPECT_FALSE(outer.get<Residency>().collision);
  EXPECT_TRUE(outer.get<Residency>().render);EXPECT_FALSE(outer.get<Residency>().simulation);
  Physics::System physics;physics.step(world,1.0f/120);
  EXPECT_LT(center.get<Transform>().position.y,2);EXPECT_EQ(outer.get<Transform>().position.y,2);
}
TEST(WorldRuntime, UnloadReloadDeletionAndCrossChunkMigrationPersist) {
  auto store=std::make_shared<Store>();
  ObjectRef moved=reference({0,0});
  {
    Jade::World world;configure(world,store);
    auto source=world.streaming().addSource({{8,0,8}});world.streaming().barrier();
    auto entity=world.streaming().resolve(moved);ASSERT_TRUE(entity);
    entity.get<Transform>().position.x=24;world.streaming().migrate();world.save();
    EXPECT_EQ(store->data.objects.at(moved.object).coord,(ChunkCoord{1,0}));
    world.destroy(world.streaming().resolve(reference({-1,0})));world.save();
    world.streaming().setSource(source,{{152,0,8}});world.streaming().barrier();
    for(int i=0;i<40;++i)world.updateStreaming(0);
    EXPECT_FALSE(entity);
    world.streaming().setSource(source,{{8,0,8}});world.streaming().barrier();
    auto reloaded=world.streaming().resolve(moved);ASSERT_TRUE(reloaded);EXPECT_NE(reloaded,entity);EXPECT_EQ(reloaded.get<Transform>().position.x,24);
    EXPECT_FALSE(world.streaming().resolve(reference({-1,0})));world.save();
  }
  Jade::World restored;configure(restored,store);restored.streaming().addSource({{24,0,8}});restored.streaming().barrier();
  ASSERT_TRUE(restored.streaming().resolve(moved));EXPECT_EQ(restored.streaming().resolve(moved).get<Transform>().position.x,24);
  EXPECT_FALSE(restored.streaming().resolve(reference({-1,0})));
}
TEST(WorldRuntime, SaveFailureRetainsOnlyRuntimeCopyAndCanRetry) {
  auto store=std::make_shared<Store>();Jade::World world;configure(world,store);
  auto source=world.streaming().addSource({{8,0,8}});world.streaming().barrier();
  auto entity=world.streaming().resolve(reference({0,0}));ASSERT_TRUE(entity);
  store->fail=true;world.streaming().setSource(source,{{152,0,8}});
  for(int i=0;i<40;++i)world.updateStreaming(0);
  EXPECT_TRUE(entity);EXPECT_FALSE(world.streaming().statistics().error.empty());
  store->fail=false;world.streaming().retry();
  for(int i=0;i<40;++i)world.updateStreaming(0);
  EXPECT_FALSE(entity);
}
TEST(WorldRuntime, FailedSpawnRollsBackRootChildrenAndBindings) {
  Jade::World world;StreamingSettings settings;
  auto broken=codec();broken.spawn=[](Jade::World& w,Entity root,const ObjectData&){auto child=w.create();w.setParent(child,root);throw std::runtime_error("spawn failure");};
  world.streaming().configure(settings,std::make_shared<Provider>(),{{"test",broken}});
  world.streaming().addSource({{8,0,8},0,0,0,0});
  EXPECT_THROW(world.streaming().barrier(),std::runtime_error);
  const auto all=world.registry().query<Archetype<>>();EXPECT_EQ(all.begin(),all.end());
}
TEST(WorldRuntime, AtomicFileCommitRestoresWholeSnapshotAndRejectsCorruption) {
  const auto path=std::filesystem::current_path()/"build/validation"/("world-save-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  FileWorldStore store(path);WorldState state{"file-world",{}};
  state.objects.emplace(1,ObjectState{{2,3},{1,"test","{\"value\":7}",true,true},false});store.save(state);
  FileWorldStore reopened(path);auto restored=reopened.load("file-world");
  EXPECT_EQ(restored.objects.at(1).coord,(ChunkCoord{2,3}));
  EXPECT_THROW(reopened.load("different-world"),std::runtime_error);
  std::ofstream(path/"commit.pending")<<"interrupted";
  EXPECT_EQ(reopened.load("file-world").objects.size(),1u);
  std::ofstream(path/"commit.json")<<"corrupt";
  EXPECT_THROW(reopened.load("file-world"),std::exception);
}
TEST(WorldRuntime, CrossChunkParentingIsRejectedBeforeChangingTree) {
  Jade::World world;configure(world);world.streaming().addSource({{8,0,8}});world.streaming().barrier();
  auto a=world.streaming().resolve(reference({0,0})),b=world.streaming().resolve(reference({1,0}));
  EXPECT_THROW(world.setParent(a,b),std::invalid_argument);EXPECT_FALSE(world.parent(a));
  auto frozen=world.streaming().resolve(reference({2,0}));auto child=world.create<Archetype<Transform,Collider>>();child.get<Collider>()=Collider{Box{}};
  world.setParent(child,frozen);ASSERT_TRUE(child.has<Residency>());EXPECT_FALSE(child.get<Residency>().collision);
  EXPECT_FALSE(child.get<Residency>().simulation);EXPECT_THROW(world.setParent(child),std::invalid_argument);
}
TEST(WorldRuntime, AnalyticSphereInsideTangentAndFinitePlaneBounds) {
  auto world=std::make_shared<Jade::World>();Scene view(world);Assets assets;
  auto sphere=world->create<Archetype<Transform,Geometry,Material>>();sphere.get<Geometry>()=Geometry::sphere(1);
  auto scene=Renderer::RayScene::build(Renderer::SceneSnapshot::capture(view,assets));Renderer::SurfaceHit hit;
  ASSERT_TRUE(scene.intersect({{0,0,0},{2,0,0}},hit));EXPECT_NEAR(hit.t,.5f,1e-6);
  ASSERT_TRUE(scene.intersect({{1,0,3},{0,0,-1}},hit));EXPECT_NEAR(hit.t,3,1e-5);
  sphere.get<Geometry>()=Geometry::plane({4,2});scene=Renderer::RayScene::build(Renderer::SceneSnapshot::capture(view,assets));
  EXPECT_TRUE(scene.intersect({{1.99f,2,.99f},{0,-1,0}},hit));EXPECT_FALSE(scene.intersect({{2.01f,2,0},{0,-1,0}},hit));
}
TEST(WorldRuntime, CpuPrefetchDoesNotSpawnAndPromotionRestoresSavedState) {
  Jade::World world;configure(world);
  const auto source=world.streaming().addSource({{8,0,8},2,0,0,0});world.streaming().barrier();
  EXPECT_EQ(world.streaming().statistics().data,25u);EXPECT_EQ(world.streaming().statistics().rendered,1u);
  EXPECT_TRUE(world.streaming().ready({2,0},false));EXPECT_FALSE(world.streaming().ready({2,0},true));
  EXPECT_FALSE(world.streaming().resolve(reference({2,0})));
  world.streaming().setSource(source,{{8,0,8}});world.streaming().barrier();
  auto outer=world.streaming().resolve(reference({2,0}));ASSERT_TRUE(outer);outer.get<Transform>().position.y=7;
  // Keep the edited position inside the original chunk.
  outer.get<Transform>().position.x=39;
  world.streaming().setSource(source,{{8,0,8},2,0,0,0});
  for(int i=0;i<20;++i)world.updateStreaming(0);
  EXPECT_FALSE(outer);EXPECT_TRUE(world.streaming().ready({2,0},false));
  world.streaming().setSource(source,{{8,0,8}});world.streaming().barrier();
  auto restored=world.streaming().resolve(reference({2,0}));ASSERT_TRUE(restored);EXPECT_EQ(restored.get<Transform>().position.x,39);
}
TEST(WorldRuntime, ObsoleteAsyncLoadsNeverPublishEntities) {
  JobSystem jobs({2});Jade::World world;auto provider=std::make_shared<GatedProvider>();StreamingSettings settings;settings.world="test-world";
  world.streaming().configure(settings,provider,{{"test",codec()}},{},&jobs);
  struct Release { std::shared_ptr<GatedProvider> provider;~Release(){provider->release();} } release{provider};
  auto source=world.streaming().addSource({{8,0,8},0,0,0,0});world.updateStreaming(0);
  { std::unique_lock lock(provider->mutex);ASSERT_TRUE(provider->changed.wait_for(lock,std::chrono::seconds(2),[&]{return provider->started>0;})); }
  world.streaming().setSource(source,{{168,0,8},0,0,0,0});world.updateStreaming(0);
  EXPECT_FALSE(world.streaming().resolve(reference({0,0})));EXPECT_LE(world.streaming().statistics().loading,4u);
  provider->release();world.streaming().barrier();
  EXPECT_FALSE(world.streaming().resolve(reference({0,0})));EXPECT_TRUE(world.streaming().resolve(reference({10,0})));
  EXPECT_EQ(world.streaming().statistics().data,1u);
}
TEST(WorldRuntime, MultipleSourcesUnionAndResidentBudgetAreEnforced) {
  Jade::World world;configure(world);
  world.streaming().addSource({{8,0,8},1,1,1,0});world.streaming().addSource({{40,0,8},1,1,1,0});world.streaming().barrier();
  EXPECT_EQ(world.streaming().statistics().data,15u);EXPECT_EQ(world.streaming().statistics().active,2u);
  Jade::World limited;StreamingSettings settings;settings.maxResidentChunks=1;
  limited.streaming().configure(settings,std::make_shared<Provider>(),{{"test",codec()}});
  limited.streaming().addSource({{8,0,8}});EXPECT_THROW(limited.streaming().barrier(),std::runtime_error);
  EXPECT_EQ(limited.streaming().statistics().data,0u);
  Jade::World prefetch;configure(prefetch);prefetch.streaming().addSource({{8,0,8},1,-1,-1,-1});prefetch.streaming().barrier();
  EXPECT_EQ(prefetch.streaming().statistics().data,9u);EXPECT_EQ(prefetch.streaming().statistics().rendered,0u);
  const auto empty=prefetch.registry().query<Transform>();EXPECT_EQ(empty.begin(),empty.end());
}
TEST(WorldRuntime, DeferredCommitAndPatchPreserveTraversalContract) {
  Jade::World world;auto e=world.create<Archetype<Value>>();const auto before=world.registry().revision(e);
  world.registry().patch<Value>(e,[](Value& value){value.value=9;});EXPECT_GT(world.registry().revision(e),before);
  world.registry().each<Value>([&](Entity entity,Value&){
    world.command([entity](Jade::World& w){w.destroy(entity);});EXPECT_THROW(world.commit(),std::logic_error);
  });
  EXPECT_TRUE(e);world.commit();EXPECT_FALSE(e);
}
TEST(WorldRuntime, RuntimeSchedulesWorldOnceAndSceneSwitchKeepsBodyIdentity) {
  EngineConfig config;config.window.visible=false;config.window.width=160;config.window.height=120;config.debug.axes=false;
  Application app(config);auto world=std::make_shared<Jade::World>();auto second=std::make_shared<Jade::World>();
  unsigned firstTicks=0,secondTicks=0;
  world->setFixedUpdate([&](Jade::World&,float){++firstTicks;});second->setFixedUpdate([&](Jade::World&,float){++secondTicks;});
  app.runtime().addWorld(world);app.runtime().addWorld(world);app.runtime().addWorld(second);
  auto a=std::make_shared<Scene>(world),b=std::make_shared<Scene>(world);app.runtime().setScene(a);
  auto entity=world->create<Archetype<Transform,Collider,RigidBody>>();entity.get<Collider>()=Collider{Sphere{1}};
  app.runtime().tick(1.0f/120);const auto body=world->physicsBody(entity);const auto position=entity.get<Transform>().position.y;
  EXPECT_EQ(firstTicks,1u);EXPECT_EQ(secondTicks,1u);app.runtime().setScene(b);EXPECT_EQ(world->physicsBody(entity),body);
  app.runtime().tick(1.0f/120);EXPECT_LT(entity.get<Transform>().position.y,position);EXPECT_EQ(firstTicks,2u);
  app.runtime().setScene(nullptr);app.runtime().tick(1.0f/120);EXPECT_EQ(firstTicks,3u);EXPECT_EQ(secondTicks,3u);
  app.runtime().removeWorld(world);app.runtime().tick(1.0f/120);EXPECT_EQ(firstTicks,3u);EXPECT_EQ(secondTicks,4u);
}
TEST(WorldRuntime, EditsDuringAsyncSaveRequireNewAcknowledgementBeforeRelease) {
  JobSystem jobs({2});Jade::World world;auto store=std::make_shared<GatedStore>();StreamingSettings settings;settings.world="test-world";settings.releaseDelay=0;
  world.streaming().configure(settings,std::make_shared<Provider>(),{{"test",codec()}},store,&jobs);
  struct Release {std::shared_ptr<GatedStore> store;~Release(){store->release();}} release{store};
  auto source=world.streaming().addSource({{8,0,8},0,0,0,0});world.streaming().barrier();
  auto entity=world.streaming().resolve(reference({0,0}));ASSERT_TRUE(entity);
  world.streaming().setSource(source,{{168,0,8},0,0,0,0});world.updateStreaming(0);
  {std::unique_lock lock(store->mutex);ASSERT_TRUE(store->changed.wait_for(lock,std::chrono::seconds(2),[&]{return store->started;}));}
  entity.get<Transform>().position.x=9;EXPECT_TRUE(entity);store->release();world.streaming().barrier();
  for(int i=0;i<8;++i)world.updateStreaming(0);world.save();
  EXPECT_FALSE(entity);EXPECT_EQ(std::stof(store->data.objects.at(reference({0,0}).object).data.state),9);
}
TEST(WorldRuntime, SharedPhysicsBridgePreservesSnapshotsForEachWorld) {
  Jade::World a,b;Physics::System physics;
  auto floor=a.create<Archetype<Transform,Collider>>();floor.get<Transform>().position.y=-.5f;floor.get<Collider>()=Collider{Box{{10,.5f,10}}};
  auto ball=a.create<Archetype<Transform,Collider,RigidBody>>();ball.get<Transform>().position.y=1;ball.get<Collider>()=Collider{Sphere{1}};
  auto other=b.create<Archetype<Transform,Collider,RigidBody>>();other.get<Collider>()=Collider{Sphere{1}};
  physics.step(a,1.0f/120);const auto before=physics.contactSnapshot(a);ASSERT_EQ(before.size(),1u);
  physics.step(b,1.0f/120);const auto after=physics.contactSnapshot(a);ASSERT_EQ(after.size(),1u);
  EXPECT_EQ(after[0].points[0].normalImpulse,before[0].points[0].normalImpulse);
  const auto forces=physics.forceSnapshot(a);ASSERT_EQ(forces.size(),1u);EXPECT_TRUE(forces[0].solved);EXPECT_GT(forces[0].contactForce.y,0);
  physics.reset();EXPECT_FALSE(physics.forceSnapshot(a)[0].solved);
}
TEST(WorldRuntime, BatchedAndGameplayBoundariesProduceTheSameSimulation) {
  Jade::World a,b;Physics::System physics;
  auto create=[](Jade::World& world){auto e=world.create<Archetype<Transform,Collider,RigidBody>>();e.get<Collider>()=Collider{Sphere{1}};e.get<RigidBody>().velocity={1,2,3};return e;};
  auto first=create(a),second=create(b);unsigned callbacks=0;b.setFixedUpdate([&](Jade::World&,float){++callbacks;});
  a.tick(physics,6.0/120);b.tick(physics,6.0/120);
  EXPECT_EQ(callbacks,6u);EXPECT_EQ(a.tickStatistics().substeps,6u);EXPECT_EQ(b.tickStatistics().substeps,6u);
  EXPECT_EQ(first.get<Transform>().position,second.get<Transform>().position);
  EXPECT_EQ(first.get<RigidBody>().velocity,second.get<RigidBody>().velocity);
}
TEST(WorldRuntime, PendingMigrationFreezesAndSavesTheActualDestination) {
  auto store=std::make_shared<Store>();
  {
    Jade::World world;configure(world,store);
    world.streaming().addSource({{8,0,8},0,0,0,0});world.streaming().barrier();
    auto entity=world.streaming().resolve(reference({0,0}));ASSERT_TRUE(entity);
    entity.get<Transform>().position.x=88;world.streaming().migrate();
    EXPECT_TRUE(entity);EXPECT_FALSE(entity.get<Residency>().simulation);EXPECT_FALSE(entity.get<Residency>().collision);
    world.save();EXPECT_EQ(store->data.objects.at(reference({0,0}).object).coord,(ChunkCoord{5,0}));
    world.streaming().barrier(); // The reserved identity must not duplicate during destination publication.
  }
  Jade::World restored;configure(restored,store);restored.streaming().addSource({{88,0,8},0,0,0,0});restored.streaming().barrier();
  auto entity=restored.streaming().resolve(reference({0,0}));ASSERT_TRUE(entity);EXPECT_EQ(entity.get<Transform>().position.x,88);
}
TEST(WorldRuntime, RuntimeSpawnUsesCurrentResidencyAndCannotReusePersistentIdentity) {
  Jade::World world;auto store=std::make_shared<Store>();configure(world,store);
  world.streaming().addSource({{8,0,8}});world.streaming().barrier();
  auto entity=world.create<Archetype<Transform,Collider,RigidBody>>();entity.get<Transform>().position={8,2,8};entity.get<Collider>()=Collider{Sphere{1}};
  constexpr ObjectId id=1000000000;world.streaming().track(entity,{0,0},{id,"test","8",true,true});
  ASSERT_TRUE(entity.has<PersistentObject>());EXPECT_TRUE(entity.get<Residency>().simulation);EXPECT_TRUE(entity.get<Residency>().collision);
  world.save();EXPECT_TRUE(store->data.objects.contains(id));world.destroy(entity);world.save();EXPECT_TRUE(store->data.objects.at(id).deleted);
  auto replacement=world.create();EXPECT_THROW(world.streaming().track(replacement,{0,0},{id,"test","8",true,true}),std::invalid_argument);
  StreamingSettings settings;EXPECT_THROW(world.streaming().configure(settings,std::make_shared<Provider>(),{{"test",codec()}}),std::logic_error);
}
