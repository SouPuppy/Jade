#include <Jade/Jade.hpp>
#include "TestScene.hpp"
#include "internal/Platform/Window.hpp"
#include "internal/Renderer/Device/OpenGL/OpenGLDevice.hpp"
#include "internal/Renderer/Presentation/OpenGLDisplay.hpp"
#include <gtest/gtest.h>
#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <array>

namespace {
using namespace Jade;
using namespace Jade::Renderer;
void tap(int code,int modifiers=0) {
  auto window=glfwGetCurrentContext();
  auto focus=glfwSetWindowFocusCallback(window,nullptr);glfwSetWindowFocusCallback(window,focus);focus(window,GLFW_TRUE);
  auto key=glfwSetKeyCallback(window,nullptr);glfwSetKeyCallback(window,key);
  key(window,code,0,GLFW_PRESS,modifiers);key(window,code,0,GLFW_RELEASE,modifiers);
}
std::shared_ptr<const Mesh> quad() {
  return std::make_shared<const Mesh>(Mesh::compile({{{{-1,-1,0},{0,0,1}},{{1,-1,0},{0,0,1}},
    {{1,1,0},{0,0,1}},{{-1,1,0},{0,0,1}}},{0,1,2,0,2,3}}));
}
SceneSnapshot frontScene() {
  SceneSnapshot s;CameraFrame camera;camera.position={0,0,3};camera.lens.projection=Projection::Orthographic;camera.lens.viewHeight=2;
  s.camera=camera;return s;
}
struct Screen {
  Window window;
  Jade::Detail::OpenGLDevice device;
  Jade::Detail::OpenGLDisplay display;
  Screen():window([]{WindowConfig c;c.visible=false;c.width=c.height=64;return c;}()),device(window,false),display(device) {}
  std::array<unsigned char,4> pixel(const SceneSnapshot& scene,bool hybrid,int x=32,int y=32,bool normals=false,bool cpu=false) {
    display.begin({64,64});device.drawScene(scene,{{64,64}},{normals,0,cpu,1},hybrid);
    std::array<unsigned char,4> p{};glReadBuffer(GL_BACK);glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());
    EXPECT_EQ(glGetError(),GL_NO_ERROR);return p;
  }
};
EngineConfig hidden() {
  EngineConfig c;c.window.visible=false;c.window.width=c.window.height=64;c.debug.shortcuts=true;
  c.renderer.pipeline=RenderMode::Raster;c.frameLimits=240;c.logPerformance=false;c.jobs.workerCount=2;
  return c;
}
}
TEST(Pipeline, RasterHybridDepthAlphaFrameAndNormalsAgree) {
  Screen screen;auto scene=frontScene();
  RenderInstance back{quad()};back.objectToWorld.at(2,3)=-.5f;back.unlit=true;back.material.baseColor={0,0,1};
  RenderInstance sprite{std::make_shared<const Mesh>(Mesh::compile({{{{-.5f,0,0},{0,0,1}},{{.5f,0,0},{0,0,1}},
    {{.5f,1,0},{0,0,1}},{{-.5f,1,0},{0,0,1}}},{0,1,2,0,2,3}}))};
  sprite.objectToWorld.at(0,0)=2;sprite.objectToWorld.at(1,1)=2;sprite.objectToWorld.at(1,3)=-1;
  sprite.texture=std::make_shared<const Texture>(2,1,std::vector<std::uint8_t>{255,0,0,255,0,255,0,0});sprite.unlit=true;
  scene.instances={back,sprite};
  for(bool hybrid:{false,true}) {
    const auto left=screen.pixel(scene,hybrid,16,32),right=screen.pixel(scene,hybrid,48,32);
    EXPECT_GT(left[0],150);EXPECT_LT(left[2],10);EXPECT_GT(right[2],150);EXPECT_LT(right[0],10);
    const auto normals=screen.pixel(scene,hybrid,16,32,true);EXPECT_NEAR(normals[0],128,1);EXPECT_EQ(normals[2],255);
    scene.instances[1].region={.5f,0,.5f,1};
    EXPECT_GT(screen.pixel(scene,hybrid,16,32)[2],150);
    scene.instances[1].region={0,0,1,1};
  }
}
TEST(Pipeline, RealShadowQueriesAndShadowMapsRespondToMovingOccluders) {
  Screen screen;auto scene=frontScene();
  RenderInstance ground{quad()};ground.material.baseColor={1,1,1};
  RenderInstance blocker{std::make_shared<const Mesh>(Mesh::compile(MeshData::cube(.4f)))};
  blocker.objectToWorld.at(0,3)=.4f;blocker.objectToWorld.at(2,3)=.8f;
  scene.instances={ground,blocker};RenderLight light;light.type=LightType::Directional;
  light.direction={-.4472136f,0,-.8944272f};light.intensity={4,4,4};scene.lights={light};
  for(const auto [hybrid,cpu]:{std::pair{false,false},std::pair{true,false},std::pair{true,true}}) {
    scene.instances[1].objectToWorld.at(0,3)=.4f;
    const auto dark=screen.pixel(scene,hybrid,32,32,false,cpu);
    scene.instances[1].objectToWorld.at(0,3)=1.5f;
    const auto bright=screen.pixel(scene,hybrid,32,32,false,cpu);
    EXPECT_GT(static_cast<int>(bright[0])-dark[0],70);
  }
}
TEST(Pipeline, ModeCycleConsumptionAndF5PreserveSelectedPipeline) {
  Application app(hidden());MakeTestScene(app);auto& renderer=app.runtime().renderer();
  auto consumed=app.on<KeyPressed<"I">>([]{return EventFlow::Stop;});
  tap(GLFW_KEY_I);app.Run({1});EXPECT_EQ(renderer.pipeline(),RenderMode::Raster);consumed.disconnect();
  tap(GLFW_KEY_I,GLFW_MOD_SHIFT);app.Run({1});EXPECT_EQ(renderer.pipeline(),RenderMode::Raster);
  for(const auto next:{RenderMode::Hybrid,RenderMode::RayTracing,RenderMode::None,RenderMode::Raster}) {
    tap(GLFW_KEY_I);app.Run({1});EXPECT_EQ(renderer.pipeline(),next);
    const auto settings=renderer.rayTracingSettings();
    const bool debug=renderer.normalDebugEnabled();
    tap(GLFW_KEY_F5);app.Run({1});EXPECT_EQ(renderer.pipeline(),next);
    EXPECT_EQ(renderer.normalDebugEnabled(),!debug);
    EXPECT_EQ(renderer.rayTracingSettings(),settings);
    EXPECT_EQ(renderer.sceneRenderingEnabled(),next!=RenderMode::None);
    if(next!=RenderMode::RayTracing) EXPECT_FALSE(renderer.progress().samplesPerPixel.has_value());
  }
  EXPECT_THROW(renderer.setPipeline(static_cast<RenderMode>(99)),std::invalid_argument);
}
TEST(Pipeline, EmissiveAreaLightDoesNotOccludeItsOwnShadowEndpoint) {
  Screen screen;auto scene=frontScene();RenderInstance receiver{quad()};receiver.material.baseColor={1,1,1};
  RenderInstance emitter{quad()};emitter.objectToWorld.at(0,3)=-2;emitter.objectToWorld.at(2,3)=2;
  emitter.material.emission={10,10,10};emitter.material.twoSidedEmission=true;scene.instances={receiver,emitter};
  const auto raster=screen.pixel(scene,false);
  const auto gpu=screen.pixel(scene,true);
  const auto cpu=screen.pixel(scene,true,32,32,false,true);
  EXPECT_GT(raster[0],100);EXPECT_GT(gpu[0],100);EXPECT_GT(cpu[0],100);
  EXPECT_NEAR(raster[0],gpu[0],3);EXPECT_NEAR(cpu[0],gpu[0],3);
  // Closed emissive meshes use a center approximation in realtime lighting.
  // Their surface must not block rays aimed at that source's own center.
  emitter.mesh=std::make_shared<const Mesh>(Mesh::compile(MeshData::cube(.5f)));
  scene.instances[1]=emitter;
  const auto closedRaster=screen.pixel(scene,false);
  const auto closedGpu=screen.pixel(scene,true);
  const auto closedCpu=screen.pixel(scene,true,32,32,false,true);
  EXPECT_GT(closedGpu[0],60);EXPECT_GT(closedCpu[0],60);
  EXPECT_NEAR(closedGpu[0],closedRaster[0],3);EXPECT_NEAR(closedCpu[0],closedGpu[0],3);
}
TEST(Pipeline, SoulCameraConsumesGameplayInputAndWorldKeepsUpdating) {
  Application app(hidden());auto scene=MakeTestScene(app);const auto original=scene->camera();
  const auto originalPosition=original.get<Transform>().position;
  const auto lens=original.get<Camera>();int movement=0,frames=0;
  scene->on<KeyHeld<"W">>([&]{++movement;});scene->on<FrameUpdate>([&]{++frames;});
  auto object=scene->create<Archetype<Transform,Geometry,Material>>();object.get<Geometry>().mesh=app.runtime().assets().add(MeshData::cube());
  object.add<RigidBody>();object.add<Collider>(Box{});const auto initialObjectPosition=object.get<Transform>().position;
  tap(GLFW_KEY_F2);app.Run({2});ASSERT_TRUE(app.runtime().soulMode());ASSERT_NE(app.runtime().soulCamera(),nullptr);
  const auto start=app.runtime().soulCamera()->position;
  auto window=glfwGetCurrentContext();auto key=glfwSetKeyCallback(window,nullptr);glfwSetKeyCallback(window,key);
  key(window,GLFW_KEY_W,0,GLFW_PRESS,0);app.Run({4});key(window,GLFW_KEY_W,0,GLFW_RELEASE,0);
  EXPECT_EQ(movement,0);EXPECT_GT(frames,4);EXPECT_NE(app.runtime().soulCamera()->position,start);
  EXPECT_EQ(scene->camera(),original);EXPECT_EQ(original.get<Transform>().position,originalPosition);EXPECT_EQ(original.get<Camera>(),lens);
  EXPECT_FALSE(app.runtime().paused());EXPECT_FALSE(app.runtime().soulPaths().empty());EXPECT_LE(app.runtime().soulPaths().size(),144u*11u);
  EXPECT_LT(object.get<Transform>().position.y,initialObjectPosition.y);
  tap(GLFW_KEY_O);app.Run({1});EXPECT_EQ(original.get<Camera>(),lens);
  tap(GLFW_KEY_I);app.Run({1});EXPECT_EQ(app.runtime().renderer().pipeline(),RenderMode::Hybrid);
  tap(GLFW_KEY_F2);app.Run({1});EXPECT_FALSE(app.runtime().soulMode());EXPECT_EQ(app.runtime().renderer().pipeline(),RenderMode::Hybrid);
  EXPECT_TRUE(app.runtime().soulPaths().empty());
  app.runtime().setPaused(true);tap(GLFW_KEY_F2);app.Run({1});EXPECT_TRUE(app.runtime().paused());
  tap(GLFW_KEY_ESCAPE);app.Run({1});EXPECT_FALSE(app.runtime().soulMode());EXPECT_TRUE(app.runtime().paused());
  tap(GLFW_KEY_F2);app.Run({1});app.runtime().setScene(nullptr);EXPECT_FALSE(app.runtime().soulMode());
}
TEST(Pipeline, RecordedTransportMatchesRadianceAndExplicitCpuGpuLights) {
  auto snapshot=frontScene();snapshot.instances.push_back({quad()});RenderLight light;light.type=LightType::Directional;
  light.intensity={3,2,1};light.direction={0,0,-1};snapshot.lights={light};
  auto scene=RayScene::build(snapshot);PathIntegrator integrator({4,4});IndependentSampler plain(42),recorded(42);
  plain.start(0,0);recorded.start(0,0);std::vector<PathSegment> paths;
  const auto ray=snapshot.camera->generateRay({.5f,.5f},{1,1});
  EXPECT_EQ(integrator.Li(ray,scene,plain),integrator.record(ray,scene,recorded,paths));
  EXPECT_GE(paths.size(),2u);EXPECT_EQ(paths[0].kind,PathSegmentKind::Primary);
  EXPECT_TRUE(std::any_of(paths.begin(),paths.end(),[](const auto& p){return p.kind==PathSegmentKind::ShadowVisible;}));
  RayTracingSettings settings;settings.maxBounces=4;settings.jitter=false;
  RayTracing cpu(settings);settings.device=RayDevice::GPU;ComputeRayTracer gpu(settings);Film a({1,1}),b({1,1});
  cpu.render(scene,{*snapshot.camera,{1,1},0,42,{}},a);gpu.render(scene,{*snapshot.camera,{1,1},0,42,{}},b);
  const auto ac=a.resolve(),bc=b.resolve();for(int channel=0;channel<3;++channel) EXPECT_NEAR(ac.pixels[channel],bc.pixels[channel],1);
  EXPECT_GT(ac.pixels[0],100);
}
