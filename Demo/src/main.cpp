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
  scene->use<Jade::Module::CameraControl>();
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

    Jade::ApplicationConfig config;
    config.frameLimits = 24;
    config.debug = true;
    config.axisGizmo = false;
    Jade::Application app(config);

    Jade::Renderer::RayTracingConfig rendering;
    rendering.resolutionScale = 1.0f;
    rendering.maxSamples = 0;
    rendering.maxBounces = 12;
    rendering.rrStartBounce = 4;
    rendering.workerCount = 0;
    rendering.exposureEV = 0;
    app.use<Jade::Renderer::RayTracing>(rendering);

    const auto cubeMesh = app.assets().add(Jade::Mesh::cube(2));
    const auto sphereMesh = app.assets().add(Jade::Mesh::sphere(1));
    const auto bunnyMesh = app.assets().load<Jade::Mesh>("assets/bunny.obj");

    Jade::Material floor;
    floor.baseColor = {0.65f, 0.65f, 0.65f};
    Jade::Material diffuse;
    diffuse.baseColor = {0.8f, 0.28f, 0.12f};
    Jade::Material metal;
    metal.model = Jade::MaterialModel::Metal;
    metal.baseColor = {0.95f, 0.7f, 0.3f};
    metal.roughness = 0.18f;
    Jade::Material glass;
    glass.model = Jade::MaterialModel::Dielectric;
    glass.baseColor = {1, 1, 1};
    glass.roughness = 0;
    glass.ior = 1.5f;
    Jade::Material lamp;
    lamp.baseColor = {0, 0, 0};
    lamp.emission = {14, 12, 10};

    // Scene I: illumination comes exclusively from the ceiling mesh.
    auto showroom = CreateScene({4, 3.5f, 12});
    const auto &bunnyVertices = app.assets().get(bunnyMesh).vertices;
    const float bunnyBottom =
        std::min_element(bunnyVertices.begin(), bunnyVertices.end(), [](const auto &a, const auto &b) {
          return a.position.y < b.position.y;
        })->position.y;
    Instance(*showroom, cubeMesh, {0, -2, 0}, floor, {5, 0.1f, 4});
    Instance(*showroom, cubeMesh, {-2.2f, -1.1f, 0}, metal, {0.8f, 0.8f, 0.8f});
    Instance(*showroom, bunnyMesh, {0, -1.9f - bunnyBottom * 16, 0}, diffuse, {16, 16, 16});
    Instance(*showroom, sphereMesh, {2.2f, -0.9f, 0}, glass);
    Instance(*showroom, cubeMesh, {0, 4, 0}, lamp, {2, 0.05f, 1.5f});

    // Scene II: the sun is geometry with emission. P freezes the animation.
    auto hierarchy = CreateScene({7, 5, 11});
    auto sun = Instance(*hierarchy, sphereMesh, {}, lamp, {0.7f, 0.7f, 0.7f});
    Jade::Material earthMaterial;
    earthMaterial.baseColor = {0.1f, 0.35f, 0.8f};
    auto earth = Instance(*hierarchy, sphereMesh, {4, 0, 0}, earthMaterial, {0.5f, 0.5f, 0.5f});
    hierarchy->setParent(earth, sun);
    Jade::Material moonMaterial;
    moonMaterial.baseColor = {0.6f, 0.6f, 0.6f};
    auto moon = Instance(*hierarchy, sphereMesh, {3, 0, 0}, moonMaterial, {0.35f, 0.35f, 0.35f});
    hierarchy->setParent(moon, earth);
    bool animate = true;
    hierarchy->on<Jade::FrameUpdate>([sun, earth, &animate](const auto &frame) {
      if (animate) {
        Jade::Motion::yaw(sun.get<Jade::Transform>(), 12 * frame.dt);
        Jade::Motion::yaw(earth.get<Jade::Transform>(), 35 * frame.dt);
      }
    });
    hierarchy->on<Jade::KeyPressed<"P">>([&] { animate = !animate; });

    app.on<Jade::KeyPressed<"1">>([&] {
      app.setScene(showroom);
      return Jade::EventFlow::Stop;
    });
    app.on<Jade::KeyPressed<"2">>([&] {
      app.setScene(hierarchy);
      return Jade::EventFlow::Stop;
    });

    app.setScene(showroom);
    app.Run(run);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
