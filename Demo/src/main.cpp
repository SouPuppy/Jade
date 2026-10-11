#include <Jade/Jade.hpp>

#include <algorithm>
#include <charconv>
#include <iostream>
#include <string_view>

namespace {

std::shared_ptr<Jade::Scene> CreateScene(Jade::Vec3 eye)
{
  auto scene = std::make_shared<Jade::Scene>();
  auto camera = scene->create();
  auto &pose = camera.add<Jade::Transform>();
  pose.position = eye;
  Jade::Motion::lookAt(pose, {0, 0, 0});
  camera.add<Jade::Camera>();
  scene->setCamera(camera);
  scene->use<Jade::Module::FreeCameraControl>();
  return scene;
}

Jade::Entity Instance(Jade::Scene &scene, Jade::MeshHandle mesh, Jade::Vec3 position, Jade::Material material,
                      Jade::Vec3 scale = {1, 1, 1})
{
  const auto entity = scene.create();
  auto &pose = entity.add<Jade::Transform>();
  pose.position = position;
  pose.scale = scale;
  entity.add<Jade::Geometry>(mesh);
  entity.add<Jade::Material>(material);
  return entity;
}

using Object = Jade::Archetype<Jade::Geometry, Jade::Transform, Jade::Material>;
using Physical = Jade::Archetype<Jade::Transform, Jade::Collider, Jade::RigidBody>;
class StackBox : public Jade::Archetype<Object, Physical> {
  using Base = Jade::Archetype<Object, Physical>;
public:
  explicit StackBox(Jade::Entity entity) : Base(entity) { add<Jade::Collider>(Jade::Box{}); }
};
std::shared_ptr<Jade::Scene> CreateStack(Jade::Runtime& runtime, Jade::MeshHandle mesh)
{
  auto scene = CreateScene({7, 5, 11});
  Instance(*scene, mesh, {0, -0.5f, 0}, Jade::Materials::Diffuse({0.6f, 0.6f, 0.6f}), {12, 1, 12})
    .add<Jade::Collider>(Jade::Box{});
  Jade::Entity top;
  for (int layer = 0; layer < 3; ++layer) {
    const int width = 5 - 2 * layer;
    for (int i = 0; i < width; ++i) {
      auto box = scene->create<StackBox>();
      box.get<Jade::Geometry>().mesh = mesh;
      box.get<Jade::Material>() = Jade::Materials::Diffuse({0.75f, 0.35f + 0.15f * layer, 0.15f});
      box.get<Jade::Transform>().position = {static_cast<float>(i - width / 2), 0.6f + 1.1f * layer, 0};
      top = box;
    }
  }
  // Retain a fixed emissive mesh so this scene can also be inspected in RayTracing.
  Instance(*scene, mesh, {0, 6, 0}, Jade::Materials::Emissive({18, 18, 18}), {5, 0.1f, 5});
  scene->on<Jade::KeyPressed<"K">>([&runtime, scenePtr = scene.get(), top] {
    runtime.physics().applyImpulse(*scenePtr, top, {2.5f, 0, 0}, top.get<Jade::Transform>().position + Jade::Vec3{0, 0.5f, 0});
  });
  scene->on<Jade::KeyPressed<"P">>([&runtime] { runtime.setPaused(!runtime.paused()); });
  return scene;
}

} // namespace

int main(int argc, char **argv)
{
  try {
    Jade::RunOptions run;
    if (argc == 3 && std::string_view(argv[1]) == "--frames") {
      const std::string_view count(argv[2]);
      const auto [end, error] = std::from_chars(count.data(), count.data() + count.size(), run.frameLimit);
      if (error != std::errc{} || end != count.data() + count.size() || run.frameLimit == 0)
        throw std::invalid_argument("--frames requires a positive frame count.");
    } else if (argc != 1)
      throw std::invalid_argument("Usage: JadeDemo [--frames N]");

    Jade::EngineConfig config;
    config.frameLimits = 24;
    config.debug.shortcuts = true;
    config.debug.axes = false;
    config.renderer.mode = Jade::Renderer::Mode::Wireframe;
    config.renderer.rayTracing.resolutionScale = 0.5f;
    config.renderer.rayTracing.maxSamples = 0;
    config.renderer.rayTracing.maxBounces = 12;
    config.renderer.rayTracing.rrStartBounce = 4;
    config.renderer.rayTracing.workerCount = 0;
    config.renderer.rayTracing.exposureEV = 0;
    Jade::Application app(config);

    const auto cubeMesh = app.runtime().assets().add(Jade::MeshData::cube(2));
    const auto sphereMesh = app.runtime().assets().add(Jade::MeshData::sphere(1));
    const auto stackMesh = app.runtime().assets().add(Jade::MeshData::cube(1));
    const auto bunnyMesh = app.runtime().assets().load<Jade::Mesh>("assets/bunny.obj");

    const auto floor = Jade::Materials::Diffuse({0.65f, 0.65f, 0.65f});
    const auto diffuse = Jade::Materials::Diffuse({0.8f, 0.28f, 0.12f});
    const auto metal = Jade::Materials::Gold();
    const auto glass = Jade::Materials::Glass();
    const auto lamp = Jade::Materials::Emissive({14, 12, 10});

    // Scene I: illumination comes exclusively from the ceiling mesh.
    auto showroom = CreateScene({4, 3.5f, 12});
    const auto &bunnyVertices = app.runtime().assets().get(bunnyMesh).data().vertices;
    const float bunnyBottom =
        std::min_element(bunnyVertices.begin(), bunnyVertices.end(), [](const auto &a, const auto &b) {
          return a.position.y < b.position.y;
        })->position.y;
    Instance(*showroom, cubeMesh, {0, -2, 0}, floor, {5, 0.1f, 4})
        .add<Jade::Collider>(Jade::Box{{1, 1, 1}});
    Instance(*showroom, cubeMesh, {-2.2f, -1.1f, 0}, metal, {0.8f, 0.8f, 0.8f})
        .add<Jade::Collider>(Jade::Box{{1, 1, 1}});
    Instance(*showroom, bunnyMesh, {0, -1.9f - bunnyBottom * 16, 0}, diffuse, {16, 16, 16});
    Instance(*showroom, sphereMesh, {2.2f, -0.9f, 0}, glass).add<Jade::Collider>(Jade::Sphere{1});
    Instance(*showroom, cubeMesh, {0, 4, 0}, lamp, {2, 0.05f, 1.5f})
        .add<Jade::Collider>(Jade::Box{{1, 1, 1}});

    // Scene II: the sun emits light; a fixed area emitter lights the camera-facing planets.
    // P freezes the animation.
    auto hierarchy = CreateScene({7, 5, 11});
    auto sun = Instance(*hierarchy, sphereMesh, {}, lamp, {0.7f, 0.7f, 0.7f});
    sun.add<Jade::Collider>(Jade::Sphere{1});
    const auto earthMaterial = Jade::Materials::Diffuse({0.1f, 0.35f, 0.8f});
    auto earth = Instance(*hierarchy, sphereMesh, {4, 0, 0}, earthMaterial, {0.5f, 0.5f, 0.5f});
    earth.add<Jade::Collider>(Jade::Sphere{1});
    hierarchy->setParent(earth, sun);
    const auto moonMaterial = Jade::Materials::Diffuse({0.6f, 0.6f, 0.6f});
    auto moon = Instance(*hierarchy, sphereMesh, {3, 0, 0}, moonMaterial, {0.35f, 0.35f, 0.35f});
    moon.add<Jade::Collider>(Jade::Sphere{1});
    hierarchy->setParent(moon, earth);
    Instance(*hierarchy, cubeMesh, {3, 5, 6}, Jade::Materials::Emissive({18, 18, 18}), {2, 0.05f, 1.5f})
        .add<Jade::Collider>(Jade::Box{{1, 1, 1}});
    bool animate = true;
    hierarchy->on<Jade::FrameUpdate>([sun, earth, &animate](const auto &frame) {
      if (animate) {
        Jade::Motion::yaw(sun.get<Jade::Transform>(), 12 * frame.dt);
        Jade::Motion::yaw(earth.get<Jade::Transform>(), 35 * frame.dt);
      }
    });
    hierarchy->on<Jade::KeyPressed<"P">>([&] { animate = !animate; });
    auto stack = CreateStack(app.runtime(), stackMesh);

    app.on<Jade::KeyPressed<"1">>([&] {
      app.runtime().setPaused(false);
      app.runtime().setScene(showroom);
      return Jade::EventFlow::Stop;
    });
    app.on<Jade::KeyPressed<"2">>([&] {
      app.runtime().setPaused(false);
      app.runtime().setScene(hierarchy);
      return Jade::EventFlow::Stop;
    });

    app.on<Jade::KeyPressed<"3">>([&] {
      app.runtime().setPaused(false);
      app.runtime().renderer().setMode(Jade::Renderer::Mode::Wireframe);
      app.runtime().setScene(stack);
      return Jade::EventFlow::Stop;
    });
    app.on<Jade::KeyPressed<"R">>([&] {
      stack = CreateStack(app.runtime(), stackMesh);
      app.runtime().setPaused(false);
      app.runtime().setScene(stack);
      return Jade::EventFlow::Stop;
    });

    app.runtime().setScene(stack);
    app.Run(run);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
