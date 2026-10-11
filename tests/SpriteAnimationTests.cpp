#include <Jade/Jade.hpp>
#include "internal/Renderer/RaySceneAccess.hpp"
#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace Jade;
using namespace Jade::Renderer;
Entity sprite(Scene& scene, TextureHandle texture) {
  auto entity = scene.create<Archetype<Transform, SpriteRenderer>>();
  entity.get<SpriteRenderer>().frame.texture = texture;
  entity.get<SpriteRenderer>().size = {2,1};
  entity.get<SpriteRenderer>().billboard = false;
  return entity;
}
struct SpriteFixture {
  Scene scene;
  Assets assets;
  Entity camera, hero;
  SpriteFixture() {
    camera = scene.create<Archetype<Transform, Camera>>();
    camera.get<Transform>().position = {0,0.5f,5};
    camera.get<Camera>().projection = Projection::Orthographic;
    camera.get<Camera>().viewHeight = 2;
    scene.setCamera(camera);
    hero = sprite(scene, assets.add(Texture(2,2,{255,0,0,255, 0,255,0,0, 0,0,255,128, 255,255,255,127})));
  }
};
}

TEST(Texture, ValidatesStorageAndConvertsSrgbWithoutPremultiplication) {
  EXPECT_THROW(Texture(0,1,{}), std::invalid_argument);
  EXPECT_THROW(Texture(2,1,{0,0,0,0}), std::invalid_argument);
  Texture source(2,1,{128,64,255,128, 255,0,0,0});
  const auto texel = source.sample({0,0});
  EXPECT_NEAR(texel.color.r, 0.21586f, 0.00001f);
  EXPECT_NEAR(texel.color.g, 0.05127f, 0.00001f);
  EXPECT_EQ(texel.color.b,1);
  EXPECT_NEAR(texel.alpha,128.0f/255,0.000001f);
  EXPECT_EQ(source.sample({1,1}).alpha,0);
  EXPECT_EQ(source.sample({1,1}).color.r,1);
  EXPECT_THROW(source.sample({NAN,0}), std::invalid_argument);
}

TEST(SpriteAnimation, HandlesVariableDurationsLoopPauseAndCompletion) {
  Assets assets;
  Scene scene;
  auto entity = scene.create<Archetype<Animator, SpriteRenderer>>();
  AnimationClip clip{{{{},{0,0,0.25f,1},0.125f}, {{},{0.25f,0,0.25f,1},0.25f}, {{},{0.5f,0,0.25f,1},0.125f}},true};
  auto& animator = entity.get<Animator>();
  animator.play(assets.add(clip));
  Animation::System system;
  system.update(scene,assets,0.125f);
  EXPECT_EQ(animator.frame,1u);
  EXPECT_EQ(entity.get<SpriteRenderer>().frame.region.x,0.25f);
  system.update(scene,assets,0.25f);
  EXPECT_EQ(animator.frame,2u);
  system.update(scene,assets,10.125f);
  EXPECT_EQ(animator.frame,0u);
  EXPECT_DOUBLE_EQ(animator.time,0);
  animator.paused = true;
  system.update(scene,assets,5);
  EXPECT_DOUBLE_EQ(animator.time,0);
  clip.loop = false;
  animator.paused = false;
  animator.play(assets.add(clip));
  system.update(scene,assets,2);
  EXPECT_TRUE(animator.finished);
  EXPECT_EQ(animator.frame,2u);
  EXPECT_DOUBLE_EQ(animator.time,0.5);
}

TEST(SpriteAnimation, PreservesPhaseAcrossDirectionAndResetsOnStateChange) {
  Assets assets;
  Scene scene;
  auto entity = scene.create<Archetype<Animator, SpriteRenderer>>();
  const auto first = assets.add(AnimationClip{{SpriteFrame{{},{},0.5f}, SpriteFrame{{},{},0.5f}}});
  const auto second = assets.add(AnimationClip{{SpriteFrame{{},{},0.5f}, SpriteFrame{{},{},0.5f}}});
  auto& animator = entity.get<Animator>();
  animator.play(first);
  Animation::System system;
  system.update(scene,assets,0.75f);
  animator.play(second,true);
  system.update(scene,assets,0);
  EXPECT_EQ(animator.frame,1u);
  EXPECT_DOUBLE_EQ(animator.time,0.75);
  animator.play(first);
  system.update(scene,assets,0);
  EXPECT_EQ(animator.frame,0u);
}

TEST(SpriteAnimation, RejectsInvalidClipsAndTime) {
  Assets assets;
  Scene scene;
  auto entity = scene.create<Archetype<Animator, SpriteRenderer>>();
  Animation::System system;
  entity.get<Animator>().clip = assets.add(AnimationClip{});
  EXPECT_THROW(system.update(scene,assets,0),std::invalid_argument);
  entity.get<Animator>().clip = assets.add(AnimationClip{{SpriteFrame{{},{},0}}});
  EXPECT_THROW(system.update(scene,assets,0),std::invalid_argument);
  EXPECT_THROW(system.update(scene,assets,-1),std::invalid_argument);
}

TEST(SpriteRenderer, TransparentPixelsDoNotOccludeAndFrameChangesReuseGeometry) {
  SpriteFixture f;
  RaySceneCompiler compiler;
  const auto snapshot = SceneSnapshot::capture(f.scene,f.assets);
  const auto rays = compiler.compile(snapshot);
  SurfaceHit hit;
  EXPECT_TRUE(rays.intersect({{-0.5f,0.75f,2},{0,0,-1},0,3},hit));
  EXPECT_EQ(rays.spriteSample(hit).color, (LinearRGB{1,0,0}));
  EXPECT_FALSE(rays.occluded({{0.5f,0.75f,2},{0,0,-1},0,3}));
  EXPECT_TRUE(rays.occluded({{-0.5f,0.25f,2},{0,0,-1},0,3}));
  EXPECT_FALSE(rays.occluded({{0.5f,0.25f,-2},{0,0,1},0,3}));
  f.hero.get<SpriteRenderer>().frame.region = {0,0,0.5f,1};
  const auto changed = compiler.compile(SceneSnapshot::capture(f.scene,f.assets));
  EXPECT_EQ(Detail::RaySceneAccess::geometryIdentity(rays),Detail::RaySceneAccess::geometryIdentity(changed));
  EXPECT_TRUE(changed.intersect({{0.5f,0.75f,2},{0,0,-1},0,3},hit));
  EXPECT_EQ(changed.spriteSample(hit).color, (LinearRGB{1,0,0}));
  // Detached old frames still contain the old UV and shared texture.
  EXPECT_FALSE(rays.occluded({{0.5f,0.75f,2},{0,0,-1},0,3}));
  EXPECT_EQ(snapshot.instances.back().mesh,SceneSnapshot::capture(f.scene,f.assets).instances.back().mesh);
}

TEST(SpriteRenderer, BillboardRemainsVerticalAndKeepsItsFootAnchor) {
  SpriteFixture f;
  auto& sprite = f.hero.get<SpriteRenderer>();
  sprite.billboard = true;
  sprite.frame.pivot = {0.5f,0.25f};
  f.hero.get<Transform>().position = {2,0,3};
  for (int direction=0;direction<8;++direction) {
    auto& pose = f.camera.get<Transform>();
    pose.position = Quaternion::angleAxis({0,1,0},direction*45).rotate({0,5,5}) + Vec3{2,0,3};
    Motion::lookAt(pose,{2,0,3});
    const auto snapshot = SceneSnapshot::capture(f.scene,f.assets);
    const auto& model = snapshot.instances.back().objectToWorld;
    EXPECT_NEAR(model.at(1,0),0,0.00001f);
    EXPECT_NEAR(model.at(0,1),0,0.00001f);
    EXPECT_NEAR(model.at(2,1),0,0.00001f);
    EXPECT_NEAR(model.at(1,1)*0.25f+model.at(1,3),0,0.00001f);
    EXPECT_NEAR(model.at(0,3),2,0.00001f);
    EXPECT_NEAR(model.at(2,3),3,0.00001f);
  }
}

TEST(SpriteRenderer, CpuAndGpuAgreeOnCutoutColorAndFrameUpdates) {
  SpriteFixture f;
  auto background = f.scene.create<Archetype<Transform, SpriteRenderer>>();
  background.get<Transform>().position = {0,0,-1};
  background.get<SpriteRenderer>().size = {3,2};
  background.get<SpriteRenderer>().billboard = false;
  background.get<SpriteRenderer>().frame.texture = f.assets.add(Texture(1,1,{0,255,255,255}));
  for (auto mode : {RayMode::RayCast,RayMode::PathTrace}) {
    RayTracingSettings settings;
    settings.jitter = false;
    settings.mode = mode;
    RayTracing cpu(settings);
    settings.device = RayDevice::GPU;
    ComputeRayTracer gpu(settings);
    for (const auto region : {SpriteRegion{},SpriteRegion{0.5f,0,0.5f,1},SpriteRegion{0,0,0.5f,1}}) {
      f.hero.get<SpriteRenderer>().frame.region = region;
      const auto snapshot = SceneSnapshot::capture(f.scene,f.assets);
      const auto scene = RayScene::build(snapshot);
      Film expected({31,23}), actual(expected.size());
      const RenderRequest request{*snapshot.camera,expected.size(),0,42,{}};
      cpu.render(scene,request,expected);
      gpu.render(scene,request,actual);
      ASSERT_EQ(actual.samples(),1u);
      for (std::size_t i=0;i<actual.pixels().size();++i) {
        EXPECT_NEAR(actual.pixels()[i].r,expected.pixels()[i].r,0.0001f) << i;
        EXPECT_NEAR(actual.pixels()[i].g,expected.pixels()[i].g,0.0001f) << i;
        EXPECT_NEAR(actual.pixels()[i].b,expected.pixels()[i].b,0.0001f) << i;
      }
    }
  }
}

TEST(CharacterController, StopsAtWallsAndSlidesWithoutRigidBody) {
  Scene scene;
  auto player = scene.create<Archetype<Transform, Collider, CharacterController>>();
  player.get<Collider>() = Collider{Box{{0.2f,0.3f,0.2f}}};
  auto wall = scene.create<Archetype<Transform, Collider>>();
  wall.get<Transform>().position = {1,0,0};
  wall.get<Collider>() = Collider{Box{{0.1f,1,10}}};
  const auto actual = player.get<CharacterController>().move(scene,player,{3,0,2});
  EXPECT_LE(actual.x,0.7001f);
  EXPECT_GE(actual.x,0.69f);
  EXPECT_NEAR(actual.z,2,0.0001f);
  EXPECT_EQ(actual.y,0);
  EXPECT_FALSE(player.has<RigidBody>());
  EXPECT_THROW(player.get<CharacterController>().move(scene,player,{INFINITY,0,0}),std::invalid_argument);
  const auto parent = scene.create();
  scene.setParent(player,parent);
  EXPECT_THROW(player.get<CharacterController>().move(scene,player,{1,0,0}),std::invalid_argument);
}
