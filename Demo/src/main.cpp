#include <Jade/Jade.hpp>

int main()
{
  constexpr float moveSpeed = 3.0f;
  constexpr float fastMoveSpeed = 6.0f;
  constexpr float slowMoveSpeed = 1.0f;
  constexpr double rollSpeed = 60.0;
  constexpr double mouseSensitivity = 0.1;
  constexpr double zoomSensitivity = 1.0;

  Jade::ApplicationConfig config;
  config.frameLimits = 24;
  Jade::Application app(config);

  auto camera = app.camera();
  auto& pose = camera.get<Jade::Transform>();
  auto& lens = camera.get<Jade::Camera>();
  pose.position = {4, 3, 8};
  Jade::Motion::lookAt(pose, {0, 0, 0});

  const auto cubeMesh = app.assets().add(Jade::Mesh::cube(2.0f));
  const auto bunnyMesh = app.assets().load<Jade::Mesh>("assets/bunny.obj");
  auto cube = app.world().create();
  cube.add<Jade::Transform>().position = {-1.5f, 0, 0};
  cube.add<Jade::MeshRenderer>(cubeMesh, Jade::Color{0.45f, 0.75f, 1.0f, 1});
  auto bunny = app.world().create();
  auto& bunnyPose = bunny.add<Jade::Transform>();
  bunnyPose.scale = {16, 16, 16};
  bunnyPose.position = {1.9f, -1.7f, 0};
  bunny.add<Jade::MeshRenderer>(bunnyMesh, Jade::Color{1.0f, 0.8f, 0.55f, 1});
  // Unit XYZ axes at the world origin are enabled by default; F3 toggles them.

  bool wireframe = false;
  app.setWireframe(wireframe);

  // WASD: speed is world units per second, independent of frameLimits.
  app.on<Jade::KeyHeld<"w">>([&pose](const auto& event) {
    Jade::Motion::forward(pose, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"s">>([&pose](const auto& event) {
    Jade::Motion::backward(pose, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"a">>([&pose](const auto& event) {
    Jade::Motion::left(pose, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"d">>([&pose](const auto& event) {
    Jade::Motion::right(pose, moveSpeed * event.dt);
  });

  // Arrow keys provide the same movement as WASD.
  app.on<Jade::KeyHeld<"Up">>([&pose](const auto& event) {
    Jade::Motion::forward(pose, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"Down">>([&pose](const auto& event) {
    Jade::Motion::backward(pose, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"Left">>([&pose](const auto& event) {
    Jade::Motion::left(pose, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"Right">>([&pose](const auto& event) {
    Jade::Motion::right(pose, moveSpeed * event.dt);
  });

  // Modifier bindings are exact: Shift+W is fast, Ctrl+W is slow.
  app.on<Jade::KeyHeld<"S-w">>([&pose](const auto& event) {
    Jade::Motion::forward(pose, fastMoveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"C-w">>([&pose](const auto& event) {
    Jade::Motion::forward(pose, slowMoveSpeed * event.dt);
  });

  // Space / Ctrl+Space move along the world Y axis.
  app.on<Jade::KeyHeld<"Space">>([&pose](const auto& event) {
    Jade::Motion::up(pose, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"C-Space">>([&pose](const auto& event) {
    Jade::Motion::down(pose, moveSpeed * event.dt);
  });

  // Q / E roll around the viewing direction, in degrees per second.
  app.on<Jade::KeyHeld<"q">>([&pose](const auto& event) {
    Jade::Motion::roll(pose, -rollSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"e">>([&pose](const auto& event) {
    Jade::Motion::roll(pose, rollSpeed * event.dt);
  });

  // Rotate only while dragging with the left button. Focus loss also releases it.
  bool dragging = false;
  app.on<Jade::MousePressed<"Left">>([&dragging] {
    dragging = true;
  });
  app.on<Jade::MouseReleased<"Left">>([&dragging] {
    dragging = false;
  });
  app.on<Jade::MouseMoved>([&pose, &dragging](const auto& mouse) {
    if (dragging) {
      // Pixel deltas already describe displacement; do not multiply by dt.
      Jade::Motion::yaw(pose, mouse.dx * mouseSensitivity);
      Jade::Motion::pitch(pose, -mouse.dy * mouseSensitivity);
    }
  });
  app.on<Jade::MouseScrolled>([&lens](const auto& mouse) {
    Jade::Motion::zoom(lens, mouse.dy * zoomSensitivity);
  });

  app.on<Jade::KeyPressed<"o">>([&lens] {
    const auto projection = lens.projection == Jade::Projection::Perspective
      ? Jade::Projection::Orthographic : Jade::Projection::Perspective;
    lens.projection = projection;
    Jade::Log::Info(projection == Jade::Projection::Perspective
      ? "Camera: Perspective" : "Camera: Orthographic");
  });

  // Press F once to toggle wireframe; Escape closes the window by default.
  app.on<Jade::KeyPressed<"f">>([&app, &wireframe] {
    wireframe = !wireframe;
    app.setWireframe(wireframe);
  });

  app.Run();
}
