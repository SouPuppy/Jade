#include <Jade/Jade.hpp>

namespace {

std::shared_ptr<Jade::Scene> CreateScene(Jade::Vec3 eye)
{
  auto scene = std::make_shared<Jade::Scene>();
  auto camera = scene->create();
  auto& pose = camera.add<Jade::Transform>();
  pose.position = eye;
  Jade::Motion::lookAt(pose, {});
  camera.add<Jade::Camera>();
  scene->setCamera(camera);
  scene->use<Jade::Module::CameraControl>();
  return scene;
}

} // namespace

int main()
{
  Jade::ApplicationConfig config;
  config.frameLimits = 24;
  config.debug = true;
  Jade::Application app(config);
  const auto cubeMesh = app.assets().add(Jade::Mesh::cube(2));
  const auto sphereMesh = app.assets().add(Jade::Mesh::sphere(1));
  const auto bunnyMesh = app.assets().load<Jade::Mesh>("assets/bunny.obj");

  // Scene I
  auto showroom = CreateScene({4, 3, 8});
  auto cube = showroom->create();
  cube.add<Jade::Transform>().position = {-1.5f, 0, 0};
  cube.add<Jade::MeshRenderer>(cubeMesh, Jade::Color{0.45f, 0.75f, 1, 1});
  auto bunny = showroom->create();
  auto& pose = bunny.add<Jade::Transform>();
  pose.scale = {16, 16, 16};
  pose.position = {1.9f, -1.7f, 0};
  bunny.add<Jade::MeshRenderer>(bunnyMesh, Jade::Color{1, 0.8f, 0.55f, 1});

  // Scene II
  auto hierarchy = CreateScene({7, 5, 11});
  auto sun = hierarchy->create();
  sun.add<Jade::Transform>().scale = {0.7f, 0.7f, 0.7f};
  sun.add<Jade::MeshRenderer>(sphereMesh, Jade::Color{1, 0.75f, 0.15f, 1});
  auto earth = hierarchy->create();
  auto& earthPose = earth.add<Jade::Transform>();
  earthPose.position = {4, 0, 0};
  earthPose.scale = {0.5f, 0.5f, 0.5f};
  earth.add<Jade::MeshRenderer>(sphereMesh, Jade::Color{0.2f, 0.55f, 1, 1});
  hierarchy->setParent(earth, sun);
  auto moon = hierarchy->create();
  auto& moonPose = moon.add<Jade::Transform>();
  moonPose.position = {3, 0, 0};
  moonPose.scale = {0.35f, 0.35f, 0.35f};
  moon.add<Jade::MeshRenderer>(sphereMesh, Jade::Color{0.8f, 0.8f, 0.85f, 1});
  hierarchy->setParent(moon, earth);
  hierarchy->on<Jade::FrameUpdate>([sun, earth](const auto& frame) {
    Jade::Motion::yaw(sun.get<Jade::Transform>(), 12 * frame.dt);
    Jade::Motion::yaw(earth.get<Jade::Transform>(), 35 * frame.dt);
  });
  app.on<Jade::KeyPressed<"1">>([&] {
    app.setScene(showroom);
    return Jade::EventFlow::Stop;
  });
  app.on<Jade::KeyPressed<"2">>([&] {
    app.setScene(hierarchy);
    return Jade::EventFlow::Stop;
  });
  app.setScene(showroom);
  app.Run();
}
