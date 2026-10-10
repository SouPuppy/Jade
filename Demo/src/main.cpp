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

  auto& camera = app.create<Jade::Camera>();
  camera.setPos({4, 3, 8});
  camera.lookAt({0, 0, 0});
  camera.setPerspective(60.0f);
  // Press O to switch between perspective and orthographic projection.

  auto& cube = app.create<Jade::Object>();
  cube.setMesh(Jade::Mesh::cube(2.0f));
  cube.setPos({-1.5f, 0, 0});
  cube.setColor({0.45f, 0.75f, 1.0f, 1});

  auto& bunny = app.create<Jade::Object>();
  bunny.setMesh(Jade::Mesh::load("assets/bunny.obj"));
  // Stanford's original coordinates are small; keep the file unchanged and scale the object.
  bunny.setScale({16, 16, 16});
  bunny.setPos({1.9f, -1.7f, 0});
  bunny.setColor({1.0f, 0.8f, 0.55f, 1});
  // Unit XYZ axes at the world origin are enabled by default; F3 toggles them.

  bool wireframe = false;
  app.setWireframe(wireframe);

  // WASD: speed is world units per second, independent of frameLimits.
  app.on<Jade::KeyHeld<"w">>([&camera](const auto& event) {
    Jade::Motion::forward(camera, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"s">>([&camera](const auto& event) {
    Jade::Motion::backward(camera, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"a">>([&camera](const auto& event) {
    Jade::Motion::left(camera, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"d">>([&camera](const auto& event) {
    Jade::Motion::right(camera, moveSpeed * event.dt);
  });

  // Arrow keys provide the same movement as WASD.
  app.on<Jade::KeyHeld<"Up">>([&camera](const auto& event) {
    Jade::Motion::forward(camera, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"Down">>([&camera](const auto& event) {
    Jade::Motion::backward(camera, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"Left">>([&camera](const auto& event) {
    Jade::Motion::left(camera, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"Right">>([&camera](const auto& event) {
    Jade::Motion::right(camera, moveSpeed * event.dt);
  });

  // Modifier bindings are exact: Shift+W is fast, Ctrl+W is slow.
  app.on<Jade::KeyHeld<"S-w">>([&camera](const auto& event) {
    Jade::Motion::forward(camera, fastMoveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"C-w">>([&camera](const auto& event) {
    Jade::Motion::forward(camera, slowMoveSpeed * event.dt);
  });

  // Space / Ctrl+Space move along the world Y axis.
  app.on<Jade::KeyHeld<"Space">>([&camera](const auto& event) {
    Jade::Motion::up(camera, moveSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"C-Space">>([&camera](const auto& event) {
    Jade::Motion::down(camera, moveSpeed * event.dt);
  });

  // Q / E roll around the viewing direction, in degrees per second.
  app.on<Jade::KeyHeld<"q">>([&camera](const auto& event) {
    Jade::Motion::roll(camera, -rollSpeed * event.dt);
  });
  app.on<Jade::KeyHeld<"e">>([&camera](const auto& event) {
    Jade::Motion::roll(camera, rollSpeed * event.dt);
  });

  // Rotate only while dragging with the left button. Focus loss also releases it.
  bool dragging = false;
  app.on<Jade::MousePressed<"Left">>([&dragging] {
    dragging = true;
  });
  app.on<Jade::MouseReleased<"Left">>([&dragging] {
    dragging = false;
  });
  app.on<Jade::MouseMoved>([&camera, &dragging](const auto& mouse) {
    if (dragging) {
      // Pixel deltas already describe displacement; do not multiply by dt.
      Jade::Motion::yaw(camera, mouse.dx * mouseSensitivity);
      Jade::Motion::pitch(camera, -mouse.dy * mouseSensitivity);
    }
  });
  app.on<Jade::MouseScrolled>([&camera](const auto& mouse) {
    Jade::Motion::zoom(camera, mouse.dy * zoomSensitivity);
  });

  app.on<Jade::KeyPressed<"o">>([&camera] {
    const auto projection = camera.Mode() == Jade::Projection::Perspective
      ? Jade::Projection::Orthographic : Jade::Projection::Perspective;
    camera.setProjection(projection);
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
