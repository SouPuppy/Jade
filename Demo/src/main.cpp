#include <Jade/Jade.hpp>

#include <algorithm>
#include <charconv>
#include <iostream>
#include <string_view>
#include <utility>

namespace {
using namespace Jade;
using Object = Archetype<Geometry, Transform, Material>;
using Physical = Archetype<Transform, Collider, RigidBody>;

template<class... Components>
class Instance : public Archetype<Object, Components...> {
  using Base = Archetype<Object, Components...>;
public:
  Instance(Entity entity, MeshHandle mesh, Vec3 position, Material material, Vec3 scale = {1, 1, 1})
    : Base(entity) {
    entity.get<Geometry>().mesh = mesh;
    entity.get<Transform>() = Transform{position, {}, scale};
    entity.get<Material>() = material;
  }
};

template<class... Components>
class BoxObject : public Instance<Collider, Components...> {
  using Base = Instance<Collider, Components...>;
public:
  template<class... Args> explicit BoxObject(Entity entity, Args&&... args)
    : Base(entity, std::forward<Args>(args)...) { entity.add<Collider>(Box{}); }
};
using Cube = BoxObject<>;
using StackBox = BoxObject<Physical>;
using Model = Instance<>;

class Ball : public Instance<Collider> {
public:
  template<class... Args> explicit Ball(Entity entity, Args&&... args)
    : Instance<Collider>(entity, std::forward<Args>(args)...) { entity.add<Collider>(Sphere{1}); }
};

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

std::shared_ptr<Scene> CreateStack(Runtime& runtime, MeshHandle mesh)
{
  auto scene = CreateScene({7, 5, 11});
  scene->create<Cube>(mesh, Vec3{0, -0.5f, 0}, Materials::Diffuse({0.6f, 0.6f, 0.6f}), Vec3{12, 1, 12});
  Entity top;
  for (int layer = 0; layer < 3; ++layer) {
    const int width = 5 - 2 * layer;
    for (int i = 0; i < width; ++i)
      top = scene->create<StackBox>(mesh, Vec3{static_cast<float>(i - width / 2), 0.6f + 1.1f * layer, 0},
                                   Materials::Diffuse({0.75f, 0.35f + 0.15f * layer, 0.15f}));
  }
  scene->create<Model>(mesh, Vec3{0, 6, 0}, Materials::Emissive({18, 18, 18}), Vec3{5, 0.1f, 5});
  scene->on<KeyPressed<"K">>([&runtime, scenePtr = scene.get(), top] {
    runtime.physics().applyImpulse(*scenePtr, top, {2.5f, 0, 0}, top.get<Transform>().position + Vec3{0, 0.5f, 0});
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
    if (argc == 3 && std::string_view(argv[1]) == "--frames") {
      const std::string_view count(argv[2]);
      const auto [end, error] = std::from_chars(count.data(), count.data() + count.size(), run.frameLimit);
      if (error != std::errc{} || end != count.data() + count.size() || run.frameLimit == 0)
        throw std::invalid_argument("--frames requires a positive frame count.");
    } else if (argc != 1)
      throw std::invalid_argument("Usage: JadeDemo [--frames N]");

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
    showroom->create<Cube>(cubeMesh, Vec3{0, -2, 0}, floor, Vec3{10, 0.2f, 8});
    showroom->create<Cube>(cubeMesh, Vec3{-2.2f, -1.1f, 0}, Materials::Gold(), Vec3{1.6f, 1.6f, 1.6f});
    showroom->create<Model>(bunnyMesh, Vec3{0, -1.9f - bunnyBottom * 16, 0},
                            Materials::Diffuse({0.8f, 0.28f, 0.12f}), Vec3{16, 16, 16});
    showroom->create<Ball>(sphereMesh, Vec3{2.2f, -0.9f, 0}, Materials::Glass());
    showroom->create<Cube>(cubeMesh, Vec3{0, 4, 0}, lamp, Vec3{4, 0.1f, 3});

    auto hierarchy = CreateScene({7, 5, 11});
    auto sun = hierarchy->create<Ball>(sphereMesh, Vec3{}, lamp, Vec3{0.7f, 0.7f, 0.7f});
    auto earth = hierarchy->create<Ball>(sphereMesh, Vec3{4, 0, 0}, Materials::Diffuse({0.1f, 0.35f, 0.8f}),
                                        Vec3{0.5f, 0.5f, 0.5f});
    auto moon = hierarchy->create<Ball>(sphereMesh, Vec3{3, 0, 0}, Materials::Diffuse({0.6f, 0.6f, 0.6f}),
                                       Vec3{0.35f, 0.35f, 0.35f});
    hierarchy->setParent(earth, sun);
    hierarchy->setParent(moon, earth);
    hierarchy->create<Cube>(cubeMesh, Vec3{3, 5, 6}, Materials::Emissive({18, 18, 18}), Vec3{4, 0.1f, 3});
    bool animate = true;
    hierarchy->on<FrameUpdate>([sun, earth, &animate](const auto& frame) {
      if (animate) {
        Motion::yaw(sun.get<Transform>(), 12 * frame.dt);
        Motion::yaw(earth.get<Transform>(), 35 * frame.dt);
      }
    });
    hierarchy->on<KeyPressed<"P">>([&] { animate = !animate; });
    auto stack = CreateStack(runtime, cubeMesh);
    const auto select = [&](const auto& scene) { runtime.setPaused(false); runtime.setScene(scene); return EventFlow::Stop; };
    app.on<KeyPressed<"1">>([&] { return select(showroom); });
    app.on<KeyPressed<"2">>([&] { return select(hierarchy); });
    app.on<KeyPressed<"3">>([&] { return select(stack); });
    app.on<KeyPressed<"R">>([&] { stack = CreateStack(runtime, cubeMesh); return select(stack); });
    runtime.setScene(stack);
    app.Run(run);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
