#include <Jade/Jade.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iostream>
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

template<class T>
Entity CreateObject(Scene& scene, MeshHandle mesh, Vec3 position, Material material, Vec3 scale = {1, 1, 1})
{
  const auto entity = scene.create<T>();
  entity.get<Geometry>().mesh = mesh;
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
  auto scene = std::make_shared<Scene>();
  const auto camera = scene->create<Archetype<Transform, Camera>>();
  camera.get<Transform>().position = eye;
  Motion::lookAt(camera.get<Transform>(), {});
  scene->setCamera(camera);
  scene->use<Module::FreeCameraControl>();
  return scene;
}

std::shared_ptr<Scene> CreateStack(Runtime& runtime, MeshHandle mesh, MeshHandle sphereMesh, float gravity)
{
  auto scene = CreateScene({7, 5, 11});
  scene->physics().fields = {std::make_shared<const Physics::UniformGravity>(Vec3{0, -gravity, 0})};
  CreateBox(*scene, mesh, Vec3{0, -0.5f, 0}, Materials::Diffuse({0.6f, 0.6f, 0.6f}), Vec3{12, 1, 12});
  Entity top;
  for (int layer = 0; layer < 3; ++layer) {
    const int width = 5 - 2 * layer;
    for (int i = 0; i < width; ++i)
      top = CreateBox<StackBox>(*scene, mesh, Vec3{static_cast<float>(i - width / 2), 0.6f + 1.1f * layer, 0},
                                   Materials::Diffuse({0.75f, 0.35f + 0.15f * layer, 0.15f}));
  }
  CreateObject<Model>(*scene, mesh, Vec3{0, 6, 0}, Materials::Emissive({18, 18, 18}), Vec3{5, 0.1f, 5});
  CreateBall<DynamicBall>(*scene, sphereMesh, Vec3{3.5f, 4, 0},
    Materials::Diffuse({0.2f, 0.5f, 0.9f}), Vec3{0.5f, 0.5f, 0.5f});
  scene->on<KeyPressed<"K">>([&runtime, scenePtr = scene.get(), top] {
    runtime.physics().applyImpulse(*scenePtr, top, {2.5f, 0, 0}, top.get<Transform>().position + Vec3{0, 0.5f, 0});
  });
  scene->on<KeyPressed<"P">>([&runtime] { runtime.setPaused(!runtime.paused()); });
  return scene;
}

std::shared_ptr<Scene> CreateOrbit(Runtime& runtime, MeshHandle sphereMesh)
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
  scene->physics().fields = {std::make_shared<const Physics::BodyGravity>(std::vector<Entity>{sun, earth, moon}, 1)};
  scene->physics().integration = Physics::Integration::VelocityVerlet;
  scene->on<KeyPressed<"P">>([&runtime] { runtime.setPaused(!runtime.paused()); });
  return scene;
}

std::shared_ptr<Scene> CreateDestruction(Runtime& runtime, MeshHandle cubeMesh, MeshHandle sphereMesh)
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
  CreateObject<Model>(*scene, cubeMesh, Vec3{0, 13, 3}, Materials::Emissive({18, 18, 18}), Vec3{10, 0.2f, 5});
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
    ball.get<Transform>().position = pose.position + direction * 2.5f;
    auto& body = ball.get<RigidBody>();
    body.velocity = direction * 30;
    body.angularVelocity = {};
  });
  scene->on<KeyPressed<"P">>([&runtime] { runtime.setPaused(!runtime.paused()); });
  return scene;
}

} // namespace

int main(int argc, char** argv)
{
  using namespace Jade;
  try {
    RunOptions run;
    int initialScene = 3;
    for (int argument = 1; argument < argc; argument += 2) {
      const std::string_view option(argv[argument]);
      if (argument + 1 >= argc) throw std::invalid_argument("Usage: JadeDemo [--frames N] [--scene 1|2|3|4]");
      const std::string_view value(argv[argument + 1]);
      if (option == "--frames") {
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), run.frameLimit);
        if (error != std::errc{} || end != value.data() + value.size() || run.frameLimit == 0)
          throw std::invalid_argument("--frames requires a positive frame count.");
      } else if (option == "--scene") {
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), initialScene);
        if (error != std::errc{} || end != value.data() + value.size() || initialScene < 1 || initialScene > 4)
          throw std::invalid_argument("--scene requires 1, 2, 3 or 4.");
      } else throw std::invalid_argument("Usage: JadeDemo [--frames N] [--scene 1|2|3|4]");
    }

    EngineConfig config;
    config.debug.shortcuts = true;
    config.renderer.render = false;
    config.renderer.wireframe = true;
    config.renderer.rayTracing.resolutionScale = 0.5f;
    Application app(config);
    auto& runtime = app.runtime();
    const auto cubeMesh = runtime.assets().add(MeshData::cube(1));
    const auto sphereMesh = runtime.assets().add(MeshData::sphere(1));
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
    CreateBox(*showroom, cubeMesh, Vec3{0, 4, 0}, lamp, Vec3{4, 0.1f, 3});

    std::shared_ptr<Scene> destruction;
    auto orbit = CreateOrbit(runtime, sphereMesh);
    bool moonGravity = false;
    auto stack = CreateStack(runtime, cubeMesh, sphereMesh, 9.81f);
    const auto select = [&](const auto& scene) { runtime.setPaused(false); runtime.setScene(scene); return EventFlow::Stop; };
    app.on<KeyPressed<"1">>([&] { return select(showroom); });
    app.on<KeyPressed<"2">>([&] { return select(orbit); });
    app.on<KeyPressed<"4">>([&] {
      if (!destruction) destruction = CreateDestruction(runtime, cubeMesh, sphereMesh);
      return select(destruction);
    });
    app.on<KeyPressed<"3">>([&] { return select(stack); });
    app.on<KeyPressed<"G">>([&] {
      if (runtime.scene() != stack) return EventFlow::Continue;
      moonGravity = !moonGravity;
      stack->physics().fields = {std::make_shared<const Physics::UniformGravity>(Vec3{0, moonGravity ? -1.62f : -9.81f, 0})};
      return EventFlow::Stop;
    });
    app.on<KeyPressed<"R">>([&] {
      if (runtime.scene() == orbit) { orbit = CreateOrbit(runtime, sphereMesh); return select(orbit); }
      if (destruction && runtime.scene() == destruction) {
        destruction = CreateDestruction(runtime, cubeMesh, sphereMesh);
        return select(destruction);
      }
      if (runtime.scene() == stack) {
        stack = CreateStack(runtime, cubeMesh, sphereMesh, moonGravity ? 1.62f : 9.81f);
        return select(stack);
      }
      return EventFlow::Stop;
    });
    if (initialScene == 4) {
      destruction = CreateDestruction(runtime, cubeMesh, sphereMesh);
      runtime.setScene(destruction);
    } else runtime.setScene(initialScene == 1 ? showroom : initialScene == 2 ? orbit : stack);
    app.Run(run);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
