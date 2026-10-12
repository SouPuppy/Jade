#include <Jade/Jade.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>

namespace {
using namespace Jade;
using Object = Archetype<Geometry, Transform, Material>;
using Physical = Archetype<Transform, Collider, RigidBody>;

using Model = Object;
using Cube = Archetype<Object, Collider>;
using StackBox = Archetype<Object, Physical>;
struct Ball {
  using Components = Archetype<Object, Collider>;
};
struct DestructionBall {
  using Components = Archetype<Ball, RigidBody>;
};
using DynamicBall = Archetype<Ball, RigidBody>;

void LaunchBall(Scene& scene,Entity ball,Vec3 position,Vec3 velocity) {
  auto& world = scene.world(); const auto id = world.physicsBody(ball);
  if (id) {
    world.physicsWorld().teleport(id,{position.x,position.y,position.z},ball.get<Transform>().rotation,true);
    world.physicsWorld().setVelocity(id,{velocity.x,velocity.y,velocity.z});
  }
  // Reflect this authored launch immediately, including while physics is paused.
  ball.get<Transform>().position = position;
  ball.get<RigidBody>().velocity = velocity; ball.get<RigidBody>().angularVelocity = {};
}

MeshData CeilingLightMesh()
{
  // The emitting underside of a unit light box. Sampling its unused upper
  // face wastes roughly half the direct-light samples for objects below it.
  MeshData mesh;
  for (Vec3 p : {Vec3{-0.5f,-0.5f,-0.5f}, Vec3{0.5f,-0.5f,-0.5f},
                 Vec3{0.5f,-0.5f,0.5f}, Vec3{-0.5f,-0.5f,0.5f}})
    mesh.vertices.push_back({p, {0,-1,0}});
  mesh.indices = {0,1,2,0,2,3};
  return mesh;
}

template<class T>
Entity CreateObject(Scene& scene, MeshHandle mesh, Vec3 position, Material material, Vec3 scale = {1, 1, 1})
{
  const auto entity = scene.world().create<T>();
  entity.get<Geometry>().resource = mesh;
  entity.get<Transform>() = Transform{position, {}, scale};
  entity.get<Material>() = material;
  return entity;
}

template<class T = Cube>
Entity CreateBox(Scene& scene, MeshHandle mesh, Vec3 position, Material material, Vec3 scale = {1, 1, 1})
{
  const auto entity = CreateObject<T>(scene, mesh, position, material, scale);
  entity.get<Collider>() = Collider{Box{}};
  return entity;
}

template<class T = Ball>
Entity CreateBall(Scene& scene, MeshHandle mesh, Vec3 position, Material material, Vec3 scale = {1, 1, 1})
{
  const auto entity = CreateObject<T>(scene, mesh, position, material, scale);
  entity.get<Collider>() = Collider{Sphere{1}};
  return entity;
}

std::shared_ptr<Scene> CreateScene(Vec3 eye)
{
  auto scene = std::make_shared<Scene>(std::make_shared<World>());
  const auto camera = scene->world().create<Archetype<Transform, Camera>>();
  camera.get<Transform>().position = eye;
  Motion::lookAt(camera.get<Transform>(), {});
  scene->setCamera(camera);
  scene->use<Module::FPSCameraControl>();
  return scene;
}

std::shared_ptr<Scene> CreateStack(Runtime& runtime, MeshHandle mesh, MeshHandle sphereMesh, MeshHandle lightMesh, float gravity)
{
  auto scene = CreateScene({7, 5, 11});
  scene->world().physics().fields = {std::make_shared<const Physics::UniformGravity>(Vec3{0, -gravity, 0})};
  CreateBox(*scene, mesh, Vec3{0, -0.5f, 0}, Materials::Diffuse({0.6f, 0.6f, 0.6f}), Vec3{12, 1, 12});
  Entity top;
  for (int layer = 0; layer < 3; ++layer) {
    const int width = 5 - 2 * layer;
    for (int i = 0; i < width; ++i)
      top = CreateBox<StackBox>(*scene, mesh, Vec3{static_cast<float>(i - width / 2), 0.6f + 1.1f * layer, 0},
                                   Materials::Diffuse({0.75f, 0.35f + 0.15f * layer, 0.15f}));
  }
  CreateObject<Model>(*scene, lightMesh, Vec3{0, 6, 0}, Materials::Emissive({18, 18, 18}), Vec3{5, 0.1f, 5});
  CreateBall<DynamicBall>(*scene, sphereMesh, Vec3{3.5f, 4, 0},
    Materials::Diffuse({0.2f, 0.5f, 0.9f}), Vec3{0.5f, 0.5f, 0.5f});
  scene->on<KeyPressed<"K">>([&runtime, scenePtr = scene.get(), top] {
    runtime.physics().applyImpulse(scenePtr->world(), top, {2.5f, 0, 0}, top.get<Transform>().position + Vec3{0, 0.5f, 0});
  });
  return scene;
}

std::shared_ptr<Scene> CreateOrbit(MeshHandle sphereMesh)
{
  auto scene = CreateScene({12, 8, 18});
  constexpr double sunMass = 100, earthMass = 10, moonMass = 0.1;
  constexpr double outer = 8, inner = 0.9, pairMass = earthMass + moonMass, total = sunMass + pairMass;
  const double outerSpeed = std::sqrt(total / outer), innerSpeed = std::sqrt(pairMass / inner);
  const auto make = [&](double x, double speed, float mass, float radius, Material material) {
    const auto e = CreateBall<DynamicBall>(*scene, sphereMesh, Vec3{static_cast<float>(x), 0, 0},
      material, Vec3{radius, radius, radius});
    e.get<RigidBody>().mass = mass;
    e.get<RigidBody>().velocity = {0, 0, static_cast<float>(speed)};
    return e;
  };
  const auto sun = make(-pairMass / total * outer, -pairMass / total * outerSpeed, 100, 0.55f,
    Materials::Emissive({14, 12, 10}));
  const auto earth = make(sunMass / total * outer - moonMass / pairMass * inner,
    sunMass / total * outerSpeed - moonMass / pairMass * innerSpeed, 10, 0.25f,
    Materials::Diffuse({0.1f, 0.35f, 0.8f}));
  const auto moon = make(sunMass / total * outer + earthMass / pairMass * inner,
    sunMass / total * outerSpeed + earthMass / pairMass * innerSpeed, 0.1f, 0.08f,
    Materials::Diffuse({0.6f, 0.6f, 0.6f}));
  scene->world().physics().fields = {std::make_shared<const Physics::BodyGravity>(std::vector<Entity>{sun, earth, moon}, 1)};
  scene->world().physics().integration = Physics::Integration::VelocityVerlet;
  return scene;
}

std::shared_ptr<Scene> CreateDestruction(MeshHandle cubeMesh, MeshHandle sphereMesh, MeshHandle lightMesh)
{
  constexpr int columns = 10, rows = 10;
  // Small horizontal gaps keep unconnected columns in separate solver islands.
  constexpr float columnSpacing = 1.04f;
  auto scene = CreateScene({10, 7, 20});
  auto camera = scene->camera();
  Motion::lookAt(camera.get<Transform>(), {0, 4.6f, 0});
  CreateBox(*scene, cubeMesh, Vec3{0, -0.5f, 0}, Materials::Diffuse({0.35f, 0.38f, 0.42f}),
                      Vec3{20, 1, 20});
  for (int row = 0; row < rows; ++row) {
    const float gradient = static_cast<float>(row) / (rows - 1);
    const auto material = Materials::Diffuse({0.85f - 0.35f * gradient, 0.3f + 0.4f * gradient, 0.12f});
    for (int column = 0; column < columns; ++column)
      CreateBox<StackBox>(*scene, cubeMesh,
        Vec3{(column - (columns - 1) * 0.5f) * columnSpacing, row + 0.5f, 0}, material);
  }
  CreateObject<Model>(*scene, lightMesh, Vec3{0, 13, 3}, Materials::Emissive({18, 18, 18}), Vec3{10, 0.2f, 5});
  const auto ball = CreateBall<DestructionBall>(*scene, sphereMesh, Vec3{0, 3, 6},
    Materials::Glass());
  auto& body = ball.get<RigidBody>();
  body.mass = 20;
  body.friction = 0.35f;
  body.restitution = 0.1f;
  ball.get<RigidBody>().velocity = {0, 0, -30};
  scene->on<KeyPressed<"K">>([scenePtr = scene.get(), ball] {
    const auto& pose = scenePtr->camera().get<Transform>();
    const auto direction = Motion::getFront(pose);
    LaunchBall(*scenePtr,ball,pose.position+direction*2.5f,direction*30);
  });
  return scene;
}

std::shared_ptr<Scene> CreateWallStress(MeshHandle cubeMesh, MeshHandle sphereMesh, MeshHandle lightMesh)
{
  constexpr int columns = 100, rows = 100, walls = 10;
  constexpr float columnSpacing = 1.04f, wallSpacing = 12;
  constexpr float ballRadius = 3, launchSpeed = 60;
  constexpr float centerZ = -(walls - 1) * wallSpacing * 0.5f;
  auto scene = CreateScene({140, 110, 180});
  auto camera = scene->camera();
  Motion::lookAt(camera.get<Transform>(), {0, rows * 0.5f, centerZ});
  camera.get<Camera>().farPlane = 1000;
  auto& controls = camera.add<CameraControl>();
  controls.speed = 30;
  controls.slowSpeed = 5;
  controls.panDistance = 200;
  CreateBox(*scene, cubeMesh, Vec3{0, -0.5f, centerZ}, Materials::Diffuse({0.35f, 0.38f, 0.42f}),
    Vec3{140, 1, 160});
  // Keep columns separated initially, as in Scene 4, to avoid a single
  // 100,000-body solver island before the projectile hits the walls.
  for (int wall = 0; wall < walls; ++wall) {
    const float depth = static_cast<float>(wall) / (walls - 1);
    for (int row = 0; row < rows; ++row) {
      const float height = static_cast<float>(row) / (rows - 1);
      const auto material = Materials::Diffuse({0.85f - 0.35f * height,
        0.3f + 0.4f * height, 0.12f + 0.3f * depth});
      for (int column = 0; column < columns; ++column)
        CreateBox<StackBox>(*scene, cubeMesh,
          Vec3{(column - (columns - 1) * 0.5f) * columnSpacing, row + 0.5f, -wall * wallSpacing}, material);
    }
  }
  CreateObject<Model>(*scene, lightMesh, Vec3{0, 120, centerZ}, Materials::Emissive({18, 18, 18}),
    Vec3{110, 0.2f, 130});
  const auto ball = CreateBall<DestructionBall>(*scene, sphereMesh, Vec3{0, rows * 0.5f, 10},
    Materials::Glass(), Vec3{ballRadius, ballRadius, ballRadius});
  auto& body = ball.get<RigidBody>();
  body.mass = 3500;
  body.friction = 0.35f;
  body.restitution = 0.1f;
  body.velocity = {0, 0, -launchSpeed};
  scene->on<KeyPressed<"K">>([scenePtr = scene.get(), ball, ballRadius, launchSpeed] {
    const auto& pose = scenePtr->camera().get<Transform>();
    const auto direction = Motion::getFront(pose);
    LaunchBall(*scenePtr,ball,pose.position+direction*(ballRadius+1.5f),direction*launchSpeed);
  });
  return scene;
}

} // namespace

int main(int argc, char** argv)
{
  using namespace Jade;
  try {
    RunOptions run;
    int initialScene = 3;
    bool hidden = false;
    std::optional<Renderer::RayDevice> device;
    constexpr auto usage = "Usage: JadeDemo [--cpu|--gpu] [--hidden] [--frames N] [--scene 1|2|3|4|5]";
    for (int argument = 1; argument < argc; ++argument) {
      const std::string_view option(argv[argument]);
      if (option == "--help") { std::cout << usage << '\n'; return 0; }
      if (option == "--hidden") { hidden = true; continue; }
      if (option == "--cpu" || option == "--gpu") {
        const auto requestedDevice = option == "--gpu" ? Renderer::RayDevice::GPU : Renderer::RayDevice::CPU;
        if (device && *device != requestedDevice) throw std::invalid_argument("Choose either --cpu or --gpu.");
        device = requestedDevice;
        continue;
      }
      if (argument + 1 >= argc) throw std::invalid_argument(usage);
      const std::string_view value(argv[++argument]);
      if (option == "--frames") {
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), run.frameLimit);
        if (error != std::errc{} || end != value.data() + value.size() || run.frameLimit == 0)
          throw std::invalid_argument("--frames requires a positive frame count.");
      } else if (option == "--scene") {
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), initialScene);
        if (error != std::errc{} || end != value.data() + value.size() || initialScene < 1 || initialScene > 5)
          throw std::invalid_argument("--scene requires 1, 2, 3, 4 or 5.");
      } else throw std::invalid_argument(usage);
    }

    EngineConfig config;
    config.window.visible = !hidden;
    config.debug.shortcuts = true;
    config.renderer.rayTracing.enabled = device.has_value();
    config.renderer.wireframe = true;
    config.renderer.rayTracing.device = device.value_or(Renderer::RayDevice::CPU);
    config.renderer.rayTracing.minSamplesPerFrame = 8;
    config.renderer.rayTracing.resolutionScale = 1.0f;
    Application app(config);
    auto& runtime = app.runtime();
    const auto cubeMesh = runtime.assets().add(MeshData::cube(1));
    const auto sphereMesh = runtime.assets().add(MeshData::sphere(1));
    const auto lightMesh = runtime.assets().add(CeilingLightMesh());
    const auto bunnyMesh = runtime.assets().load<Mesh>("assets/bunny.obj");
    const auto floor = Materials::Diffuse({0.65f, 0.65f, 0.65f});
    const auto lamp = Materials::Emissive({14, 12, 10});

    auto showroom = CreateScene({4, 3.5f, 12});
    const auto& bunnyVertices = runtime.assets().get(bunnyMesh).data().vertices;
    const float bunnyBottom = std::min_element(bunnyVertices.begin(), bunnyVertices.end(), [](const auto& a, const auto& b) {
      return a.position.y < b.position.y;
    })->position.y;
    CreateBox(*showroom, cubeMesh, Vec3{0, -2, 0}, floor, Vec3{10, 0.2f, 8});
    CreateBox(*showroom, cubeMesh, Vec3{-2.2f, -1.1f, 0}, Materials::Gold(), Vec3{1.6f, 1.6f, 1.6f});
    CreateObject<Model>(*showroom, bunnyMesh, Vec3{0, -1.9f - bunnyBottom * 16, 0},
                            Materials::Diffuse({0.8f, 0.28f, 0.12f}), Vec3{16, 16, 16});
    CreateBall(*showroom, sphereMesh, Vec3{2.2f, -0.9f, 0}, Materials::Glass());
    CreateBox(*showroom, lightMesh, Vec3{0, 4, 0}, lamp, Vec3{4, 0.1f, 3});

    std::shared_ptr<Scene> destruction;
    std::shared_ptr<Scene> wallStress;
    auto orbit = CreateOrbit(sphereMesh);
    bool moonGravity = false;
    auto stack = CreateStack(runtime, cubeMesh, sphereMesh, lightMesh, 9.81f);
    const auto select = [&](const auto& scene) { runtime.setPaused(false); runtime.setScene(scene); return EventFlow::Stop; };
    app.on<KeyPressed<"1">>([&] { return select(showroom); });
    app.on<KeyPressed<"2">>([&] { return select(orbit); });
    app.on<KeyPressed<"4">>([&] {
      if (!destruction) destruction = CreateDestruction(cubeMesh, sphereMesh, lightMesh);
      return select(destruction);
    });
    app.on<KeyPressed<"5">>([&] {
      if (!wallStress) wallStress = CreateWallStress(cubeMesh, sphereMesh, lightMesh);
      return select(wallStress);
    });
    app.on<KeyPressed<"3">>([&] { return select(stack); });
    app.on<KeyPressed<"G">>([&] {
      if (runtime.scene() != stack) return EventFlow::Continue;
      moonGravity = !moonGravity;
      stack->world().physics().fields = {std::make_shared<const Physics::UniformGravity>(Vec3{0, moonGravity ? -1.62f : -9.81f, 0})};
      return EventFlow::Stop;
    });
    app.on<KeyPressed<"R">>([&] {
      if (runtime.scene() == orbit) { orbit = CreateOrbit(sphereMesh); return select(orbit); }
      if (destruction && runtime.scene() == destruction) {
        destruction = CreateDestruction(cubeMesh, sphereMesh, lightMesh);
        return select(destruction);
      }
      if (wallStress && runtime.scene() == wallStress) {
        wallStress = CreateWallStress(cubeMesh, sphereMesh, lightMesh);
        return select(wallStress);
      }
      if (runtime.scene() == stack) {
        stack = CreateStack(runtime, cubeMesh, sphereMesh, lightMesh, moonGravity ? 1.62f : 9.81f);
        return select(stack);
      }
      return EventFlow::Stop;
    });
    if (initialScene == 5) {
      wallStress = CreateWallStress(cubeMesh, sphereMesh, lightMesh);
      runtime.setScene(wallStress);
    } else if (initialScene == 4) {
      destruction = CreateDestruction(cubeMesh, sphereMesh, lightMesh);
      runtime.setScene(destruction);
    } else runtime.setScene(initialScene == 1 ? showroom : initialScene == 2 ? orbit : stack);
    app.Run(run);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
