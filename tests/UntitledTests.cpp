#include "Game.hpp"
#include "World.hpp"
#include "internal/Platform/Window.hpp"
#include "internal/Renderer/Device/OpenGL/OpenGLDevice.hpp"
#include "internal/Renderer/Presentation/OpenGLDisplay.hpp"
#include "internal/Renderer/Debug/DebugDraw.hpp"
#include <glad/gl.h>
#include <gtest/gtest.h>
#include <GLFW/glfw3.h>
#include <fstream>
#include <cmath>

namespace {
using namespace Jade;
using namespace Jade::Renderer;
EngineConfig hidden() {
  EngineConfig config;
  config.window.visible = false;
  config.window.width = 160;
  config.window.height = 120;
  config.renderer.rayTracing.enabled = false;
  config.logPerformance = false;
  return config;
}
}

TEST(UntitledGame, AllCameraStepsPreserveScreenRelativeMovementAndDiagonalSpeed) {
  Scene scene{std::make_shared<World>()};
  auto player = scene.world().create<Archetype<Transform, Collider, CharacterController>>();
  player.get<Collider>() = Collider{Box{}};
  Untitled::CameraController camera;
  Untitled::PlayerController control;
  for (int rotation = 0; rotation < 8; ++rotation) {
    player.get<Transform>().position = {};
    camera.step = rotation;
    control.update(scene.world(),player,camera,{0,1},1);
    const auto position = player.get<Transform>().position;
    EXPECT_NEAR(dot(position,camera.forward()),3,0.0001f);
    EXPECT_NEAR(dot(position,camera.right()),0,0.0001f);
    EXPECT_EQ(Untitled::relativeDirection(control.facing,camera),0);
    player.get<Transform>().position = {};
    control.update(scene.world(),player,camera,{1,1},1);
    const auto diagonal = player.get<Transform>().position;
    EXPECT_NEAR(std::hypot(diagonal.x,diagonal.z),3,0.0001f);
    EXPECT_EQ(Untitled::relativeDirection(control.facing,camera),1);
  }
  camera.step = 0;
  for (int direction = 0; direction < 8; ++direction) {
    const float angle = direction * 3.14159265358979323846f / 4;
    EXPECT_EQ(Untitled::relativeDirection({std::sin(angle),0,-std::cos(angle)},camera),direction);
  }
  camera.rotate(8);
  EXPECT_EQ(camera.step,0);
  camera.rotate(-1);
  EXPECT_EQ(camera.step,7);
}

TEST(UntitledGame, AssetCacheClipCountsStateTransitionsAndReset) {
  Application app(hidden());
  Untitled::Game game(app.runtime(),JADE_UNTITLED_ASSET_DIR);
  auto player = game.player();
  auto& animator = player.get<Animator>();
  auto& assets = app.runtime().assets();
  EXPECT_EQ(assets.get(animator.clip).frames.size(),14u);
  const auto idle = animator.clip;
  app.runtime().animation().update(game.scene()->world(),assets,0.4f);
  const auto phase = animator.time;
  game.rotate(1);
  EXPECT_NE(animator.clip,idle);
  EXPECT_DOUBLE_EQ(animator.time,phase);
  game.update({0,1},0.125f);
  EXPECT_EQ(assets.get(animator.clip).frames.size(),9u);
  EXPECT_DOUBLE_EQ(animator.time,0);
  app.runtime().animation().update(game.scene()->world(),assets,0.125f);
  EXPECT_GT(animator.frame,0u);
  game.update({},0);
  EXPECT_EQ(assets.get(animator.clip).frames.size(),14u);
  EXPECT_DOUBLE_EQ(animator.time,0);
  game.reset();
  EXPECT_EQ(player.get<Transform>().position,(Vec3{136,0.35f,136}));
  EXPECT_EQ(animator.clip,idle);
  const auto path = std::filesystem::path(JADE_UNTITLED_ASSET_DIR)/"Characters/Hero/Original/GirlSample_Walk_Down.png";
  EXPECT_EQ(assets.load<Texture>(path),assets.load<Texture>(path.parent_path()/"."/path.filename()));
  EXPECT_THROW(assets.load<Texture>(path.parent_path()/"missing.png"),std::exception);
}
TEST(UntitledGame, WalksAcrossChunkSeamsInEveryDirection) {
  Application app(hidden());
  Untitled::Game game(app.runtime(),JADE_UNTITLED_ASSET_DIR);
  auto& world=game.scene()->world();
  for (const auto input : {Vec2{1,0},Vec2{-1,0},Vec2{0,1},Vec2{0,-1},Vec2{1,1},Vec2{-1,-1}}) {
    game.reset();
    world.updateStreaming(0);
    world.streaming().barrier();
    const auto start=game.player().get<Transform>().position;
    for (int step=0;step<960;++step) {
      world.updateStreaming(1.f/120);
      game.update(input,1.f/120);
    }
    const auto end=game.player().get<Transform>().position;
    EXPECT_NEAR(std::hypot(end.x-start.x,end.z-start.z),24,.03f) << "input=" << input.x << ',' << input.y
      << " end=" << end.x << ',' << end.z;
    EXPECT_NE(Jade::chunkAt(end),Jade::chunkAt(start));
  }
}
TEST(UntitledGame, StreamingGeometryInteractionAndPersistentRestart) {
  const auto saveRoot=std::filesystem::current_path()/"build/validation"/("untitled-save-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  ObjectRef erased,moved;
  {
    Application app(hidden());Untitled::Game game(app.runtime(),JADE_UNTITLED_ASSET_DIR,saveRoot);auto& world=game.scene()->world();
    EXPECT_EQ(world.streaming().statistics().data,25u);EXPECT_EQ(world.streaming().statistics().active,9u);
    Entity low,analytic,high;
    for(auto e:world.registry().query<Untitled::BallVisual,Transform,PersistentObject>()) {
      if(e.get<Transform>().position.z!=134)continue;
      switch(e.get<Untitled::BallVisual>().representation) {case 0:low=e;break;case 1:analytic=e;break;case 2:high=e;break;}
    }
    ASSERT_TRUE(low);ASSERT_TRUE(analytic);ASSERT_TRUE(high);
    EXPECT_EQ(low.get<Geometry>().kind,GeometryKind::Mesh);EXPECT_EQ(analytic.get<Geometry>().kind,GeometryKind::Sphere);
    EXPECT_EQ(high.get<Geometry>().kind,GeometryKind::Mesh);
    erased=analytic.get<PersistentObject>().reference;moved=low.get<PersistentObject>().reference;
    game.interact();EXPECT_GT(std::hypot(analytic.get<RigidBody>().velocity.x,analytic.get<RigidBody>().velocity.z),0);
    game.interact(true);EXPECT_FALSE(analytic);
    low.get<Transform>().position.x=152;low.get<RigidBody>().velocity={1,2,3};world.streaming().migrate();world.save();
  }
  {
    Application app(hidden());Untitled::Game game(app.runtime(),JADE_UNTITLED_ASSET_DIR,saveRoot);auto& world=game.scene()->world();
    EXPECT_FALSE(world.streaming().resolve(erased));auto restored=world.streaming().resolve(moved);ASSERT_TRUE(restored);
    EXPECT_EQ(restored.get<Transform>().position.x,152);EXPECT_EQ(restored.get<RigidBody>().velocity,(Vec3{1,2,3}));
    game.player().get<Transform>().position={255,.35f,136};game.update({},0);world.updateStreaming(2);world.streaming().barrier();
    for(int i=0;i<30;++i)world.updateStreaming(0);
    EXPECT_EQ(world.streaming().statistics().data,15u);EXPECT_EQ(world.streaming().statistics().active,6u);
    game.update({1,0},.1f);EXPECT_LE(game.player().get<Transform>().position.x,255.75f);
  }
}

TEST(UntitledGame, AllSixteenSheetsHaveOnlyTheExpectedActiveFrames) {
  Assets assets;
  for (const auto action : {std::string("Walk"),std::string("ReadyIdle")}) {
    const int frameCount = action == "Walk" ? 9 : 14;
    for (const auto direction : {"Up","UpRight","Right","DownRight","Down","DownLeft","Left","UpLeft"}) {
      const auto filename = "GirlSample_" + action + "_" + direction + ".png";
      const auto handle = assets.load<Texture>(std::filesystem::path(JADE_UNTITLED_ASSET_DIR)/"Characters/Hero/Original"/filename);
      const auto& texture = assets.get(handle);
      ASSERT_EQ(texture.width(),1024);
      ASSERT_EQ(texture.height(),action == "Walk" ? 768 : 1024);
      for (int frame=0;frame<texture.height()/256*4;++frame) {
        bool visible = false;
        for (int y=0;y<256 && !visible;++y) for (int x=0;x<256;++x) {
          const auto index = (static_cast<std::size_t>(frame/4*256+y)*texture.width()+frame%4*256+x)*4+3;
          if (texture.pixels()[index] >= 128) { visible = true; break; }
        }
        EXPECT_EQ(visible,frame<frameCount) << filename << " frame " << frame;
      }
    }
  }
}

TEST(UntitledGame, KeyboardRotationIgnoresRepeatAndFocusLossStopsMovement) {
  Application app(hidden());
  Untitled::Game game(app.runtime(),JADE_UNTITLED_ASSET_DIR);
  app.runtime().setScene(game.scene());
  auto window = glfwGetCurrentContext();
  auto key = glfwSetKeyCallback(window,nullptr);
  glfwSetKeyCallback(window,key);
  auto focus = glfwSetWindowFocusCallback(window,nullptr);
  glfwSetWindowFocusCallback(window,focus);
  ASSERT_NE(key,nullptr);
  ASSERT_NE(focus,nullptr);
  focus(window,GLFW_TRUE);
  const auto initial = game.player().get<Animator>().clip;
  key(window,GLFW_KEY_E,0,GLFW_PRESS,0);
  key(window,GLFW_KEY_E,0,GLFW_REPEAT,0);
  app.Run({1});
  const auto rotated = game.player().get<Animator>().clip;
  EXPECT_NE(rotated,initial);
  app.Run({2});
  EXPECT_EQ(game.player().get<Animator>().clip,rotated);
  key(window,GLFW_KEY_W,0,GLFW_PRESS,0);
  app.Run({3});
  const auto moved = game.player().get<Transform>().position;
  EXPECT_GT(std::hypot(moved.x,moved.z),0.1f);
  app.runtime().setPaused(true);
  app.Run({3});
  EXPECT_EQ(game.player().get<Transform>().position,moved);
  app.runtime().setPaused(false);
  focus(window,GLFW_FALSE);
  app.Run({3});
  EXPECT_EQ(game.player().get<Transform>().position,moved);
  focus(window,GLFW_TRUE);
  key(window,GLFW_KEY_R,0,GLFW_PRESS,0);
  app.Run({1});
  EXPECT_EQ(game.player().get<Transform>().position,(Vec3{136,0.35f,136}));
  EXPECT_EQ(game.player().get<Animator>().clip,initial);
  app.runtime().setScene(nullptr);
}

TEST(UntitledGame, EngineDebugShortcutsRemainAvailable) {
  auto config = hidden();
  config.debug.shortcuts = true;
  Application app(config);
  Untitled::Game game(app.runtime(),JADE_UNTITLED_ASSET_DIR);
  app.runtime().setScene(game.scene());
  auto window = glfwGetCurrentContext();
  auto key = glfwSetKeyCallback(window,nullptr);
  glfwSetKeyCallback(window,key);
  auto focus = glfwSetWindowFocusCallback(window,nullptr);
  glfwSetWindowFocusCallback(window,focus);
  focus(window,GLFW_TRUE);
  key(window,GLFW_KEY_F3,0,GLFW_PRESS,0);
  key(window,GLFW_KEY_F4,0,GLFW_PRESS,0);
  key(window,GLFW_KEY_F1,0,GLFW_PRESS,0);
  app.Run({1});
  EXPECT_TRUE(app.runtime().axesEnabled());
  EXPECT_TRUE(app.runtime().aabbsEnabled());
  EXPECT_TRUE(app.runtime().physicsDebugEnabled());
  EXPECT_TRUE(app.runtime().renderer().wireframeEnabled());
  key(window,GLFW_KEY_O,0,GLFW_PRESS,0);
  app.Run({1});
  EXPECT_EQ(game.scene()->camera().get<Camera>().projection,Projection::Perspective);
  app.runtime().setScene(nullptr);
}

TEST(UntitledGame, QEOnlyRotateAndFInteractsThroughKeyboard) {
  Application app(hidden());
  Untitled::Game game(app.runtime(),JADE_UNTITLED_ASSET_DIR);
  app.runtime().setScene(game.scene());
  auto window=glfwGetCurrentContext();
  auto key=glfwSetKeyCallback(window,nullptr);glfwSetKeyCallback(window,key);
  auto focus=glfwSetWindowFocusCallback(window,nullptr);glfwSetWindowFocusCallback(window,focus);
  ASSERT_NE(key,nullptr);ASSERT_NE(focus,nullptr);focus(window,GLFW_TRUE);
  Entity target;
  for (auto e:game.scene()->world().registry().query<Untitled::BallVisual,Transform,RigidBody>())
    if(e.get<Untitled::BallVisual>().representation==1 && e.get<Transform>().position.z==134) target=e;
  ASSERT_TRUE(target);
  const auto camera=game.scene()->camera();const auto original=camera.get<Transform>().rotation;
  key(window,GLFW_KEY_E,0,GLFW_PRESS,0);app.Run({1});key(window,GLFW_KEY_E,0,GLFW_RELEASE,0);
  EXPECT_NE(camera.get<Transform>().rotation,original);
  EXPECT_LT(std::hypot(target.get<RigidBody>().velocity.x,target.get<RigidBody>().velocity.z),1e-5f);
  key(window,GLFW_KEY_Q,0,GLFW_PRESS,0);app.Run({1});key(window,GLFW_KEY_Q,0,GLFW_RELEASE,0);
  EXPECT_EQ(camera.get<Transform>().rotation,original);
  key(window,GLFW_KEY_F,0,GLFW_PRESS,0);app.Run({1});key(window,GLFW_KEY_F,0,GLFW_RELEASE,0);
  EXPECT_GT(std::hypot(target.get<RigidBody>().velocity.x,target.get<RigidBody>().velocity.z),1.f);
  EXPECT_EQ(camera.get<Transform>().rotation,original);
  app.runtime().setPaused(true);const auto velocity=target.get<RigidBody>().velocity;
  key(window,GLFW_KEY_F,0,GLFW_PRESS,0);app.Run({1});key(window,GLFW_KEY_F,0,GLFW_RELEASE,0);
  EXPECT_EQ(target.get<RigidBody>().velocity,velocity);
  app.runtime().setScene(nullptr);
}

TEST(UntitledGame, RendersPlayableSceneAndWritesPreview) {
  Application app(hidden());
  Untitled::Game game(app.runtime(),JADE_UNTITLED_ASSET_DIR);
  const auto snapshot = SceneSnapshot::capture(*game.scene(),app.runtime().assets());
  const auto scene = RayScene::build(snapshot);
  RayTracingSettings settings;
  settings.device = RayDevice::GPU;
  settings.maxBounces = 4;
  ComputeRayTracer gpu(settings);
  Film film({480,320});
  for (std::uint64_t sample = 0; sample < 32; ++sample)
    gpu.render(scene,{*snapshot.camera,film.size(),sample,91,{}},film);
  const auto image = film.resolve();
  std::filesystem::create_directories(JADE_VALIDATION_DIR);
  const auto path = std::filesystem::path(JADE_VALIDATION_DIR)/"untitled-preview.ppm";
  std::ofstream output(path,std::ios::binary);
  output << "P6\n" << image.size.width << ' ' << image.size.height << "\n255\n";
  std::size_t heroPixels = 0;
  for (std::size_t i = 0; i < image.size.pixels(); ++i) {
    output.write(reinterpret_cast<const char*>(image.pixels.data()+i*4),3);
    const int x = static_cast<int>(i % image.size.width), y = static_cast<int>(i / image.size.width);
    SurfaceHit hit;
    if (scene.intersect(snapshot.camera->generateRay({x+0.5f,y+0.5f},image.size),hit) && hit.entity == game.player().id()) ++heroPixels;
  }
  EXPECT_GT(heroPixels,100u);
  EXPECT_LT(heroPixels,3000u);
  EXPECT_TRUE(output.good());
}

TEST(UntitledGame, RealtimePipelinesRenderHeroAndExportPreviews) {
  Application app(hidden());
  Untitled::Game game(app.runtime(),JADE_UNTITLED_ASSET_DIR);
  const auto snapshot=SceneSnapshot::capture(*game.scene(),app.runtime().assets());
  WindowConfig config;config.visible=false;config.width=480;config.height=320;
  Window window(config);Detail::OpenGLDevice device(window,false);Detail::OpenGLDisplay display(device);
  std::vector<unsigned char> pixels(480*320*4);
  for(bool hybrid:{false,true}) {
    display.begin({480,320});device.drawScene(snapshot,{{480,320}},{},hybrid);
    glReadBuffer(GL_BACK);glReadPixels(0,0,480,320,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    EXPECT_EQ(glGetError(),GL_NO_ERROR);
    std::filesystem::create_directories(JADE_VALIDATION_DIR);
    std::ofstream output(std::filesystem::path(JADE_VALIDATION_DIR)/(hybrid?"untitled-hybrid.ppm":"untitled-raster.ppm"),std::ios::binary);
    output<<"P6\n480 320\n255\n";
    std::size_t lit=0,hero=0;
    const auto rayScene=RayScene::build(snapshot);
    for(int y=319;y>=0;--y) for(int x=0;x<480;++x) {
      const auto i=static_cast<std::size_t>(y)*480+x;
      output.write(reinterpret_cast<const char*>(pixels.data()+i*4),3);
      if(pixels[i*4]>20 || pixels[i*4+1]>20 || pixels[i*4+2]>20) ++lit;
      SurfaceHit hit;
      if(rayScene.intersect(snapshot.camera->generateRay({x+.5f,319-y+.5f},{480,320}),hit) && hit.entity==game.player().id()) {
        const auto texel=rayScene.spriteSample(hit);
        Film reference({1,1});reference.addSample(std::array{texel.color});
        const auto expected=reference.resolve();
        // The same unlit sprite must reach the raster framebuffer, beyond scene-hit existence.
        if(std::abs(static_cast<int>(pixels[i*4])-expected.pixels[0])<=2 &&
           std::abs(static_cast<int>(pixels[i*4+1])-expected.pixels[1])<=2 &&
           std::abs(static_cast<int>(pixels[i*4+2])-expected.pixels[2])<=2) ++hero;
      }
    }
    EXPECT_GT(lit,10000u);EXPECT_GT(hero,100u);EXPECT_TRUE(output.good());
  }
  app.runtime().setScene(game.scene());app.runtime().setSoulMode(true);app.Run({1});
  ASSERT_FALSE(app.runtime().soulPaths().empty());
  auto observed=SceneSnapshot::capture(*game.scene(),app.runtime().assets());
  const auto source=*observed.camera;
  const auto playerPosition=game.player().get<Transform>().position;
  Transform pose;pose.position=playerPosition+Vec3{8,7,12};Motion::lookAt(pose,playerPosition+Vec3{0,1,0});
  observed.camera->lens.projection=Projection::Perspective;
  observed.camera->position=pose.position;observed.camera->forward=Motion::getFront(pose);
  observed.camera->right=Motion::getRight(pose);observed.camera->up=Motion::getUp(pose);
  display.begin({480,320});device.drawScene(observed,{{480,320}},{},false);
  Detail::DebugDraw debug(device);debug.frustum(source,*observed.camera,{480,320});
  for(const auto kind:{PathSegmentKind::Primary,PathSegmentKind::Bounce,PathSegmentKind::ShadowVisible,PathSegmentKind::ShadowBlocked,PathSegmentKind::Escape}) {
    std::vector<Vec3> points;
    for(const auto& path:app.runtime().soulPaths()) if(path.kind==kind) points.insert(points.end(),{path.start,path.end});
    const auto color=kind==PathSegmentKind::Primary?Vec3{0,.8f,1}:kind==PathSegmentKind::Bounce?Vec3{1,.5f,0}:
      kind==PathSegmentKind::ShadowVisible?Vec3{.2f,1,.2f}:kind==PathSegmentKind::ShadowBlocked?Vec3{1,.15f,.15f}:Vec3{.6f,.6f,.6f};
    debug.paths(points,*observed.camera,{480,320},color);
  }
  glReadBuffer(GL_BACK);glReadPixels(0,0,480,320,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  EXPECT_EQ(glGetError(),GL_NO_ERROR);
  std::ofstream soul(std::filesystem::path(JADE_VALIDATION_DIR)/"untitled-soul.ppm",std::ios::binary);
  soul<<"P6\n480 320\n255\n";
  std::size_t cyan=0;
  for(int y=319;y>=0;--y) for(int x=0;x<480;++x) {
    const auto i=static_cast<std::size_t>(y)*480+x;
    soul.write(reinterpret_cast<const char*>(pixels.data()+i*4),3);
    if(pixels[i*4]<20 && pixels[i*4+1]>180 && pixels[i*4+2]>240) ++cyan;
  }
  EXPECT_GT(cyan,100u);EXPECT_TRUE(soul.good());app.runtime().setScene(nullptr);
}
