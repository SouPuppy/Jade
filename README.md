# Jade

Jade is a C++20 static engine library built with CMake. The current validation platform is Windows x64 / MSVC. It provides 2D Lines and Triangles, indexed 3D Object meshes with normals and basic lighting, OBJ loading, perspective/orthographic cameras, unit world axes, Motion controls, typed keyboard/mouse/frame events, and a main loop targeting 24 FPS with per-frame logs using OpenGL 3.3 Core. Applications include individual public headers or `<Jade/Jade.hpp>` and link `Jade::Jade`. Press Esc or close the window to exit.

## Build

Install the Visual Studio or Build Tools **Desktop development with C++** workload, including MSVC x64, the Windows SDK, CMake and Ninja. GLAD generation also requires Python 3.9 or newer. The first configuration downloads pinned dependencies; subsequent configurations reuse each build directory's downloads and Python environment.

Open the repository folder in Visual Studio to discover the Windows Debug / Release presets and select `JadeDemo.exe` as the startup target. For command-line builds, use an **x64 Native Tools Command Prompt** or an x64 **Developer PowerShell**:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel
& ./build/windows-debug/bin/JadeSmokeTests.exe

cmake --preset windows-release
cmake --build --preset windows-release --parallel
& ./build/windows-release/bin/JadeSmokeTests.exe
```

GoogleTest runs directly through `JadeSmokeTests`; there is no CTest or batch-script entry point. Preset outputs remain `build/windows-debug/bin/` and `build/windows-release/bin/`, with static libraries under `lib/`. Machine-specific overrides belong in the ignored `CMakeUserPresets.json`.

All maintained files use two spaces for indentation, as configured in `.editorconfig`.

## Demo and build options

```powershell
& ./build/windows-debug/bin/JadeDemo.exe

# Build the engine and tests without Demo.
cmake --preset windows-debug -B build/no-demo-debug -DJADE_BUILD_DEMO=OFF -DBUILD_TESTING=ON
cmake --build build/no-demo-debug --parallel
& ./build/no-demo-debug/bin/JadeSmokeTests.exe
```

`JADE_BUILD_DEMO` and `BUILD_TESTING` independently control Demo and tests; disable both to build only the engine and its dependencies.

## Mesh demo

Demo displays a cube and the smallest bundled Stanford Bunny through a perspective camera at 24 FPS, with filled faces initially enabled. WASD and arrow keys move the camera, Space/Ctrl+Space move up/down, Shift+W moves forward faster and Ctrl+W slower. Left-button dragging turns the camera, the wheel zooms, Q/E roll, O switches projection, F toggles wireframe and F3 toggles the world axes. A minimal application is:

```cpp
#include <Jade/Jade.hpp>

int main()
{
  Jade::ApplicationConfig config;
  config.frameLimits = 24;
  Jade::Application app(config);

  auto& camera = app.create<Jade::Camera>();
  camera.setPos({3, 3, 5});
  camera.lookAt({0, 0, 0});
  camera.setPerspective(60.0f);
  auto& cube = app.create<Jade::Object>();
  cube.setMesh(Jade::Mesh::cube(2.0f));

  // app.setWireframe(true);
  app.Run();
}
```

`Mesh::cube(2)` builds a cube centered at the local origin with side length 2. The default camera already uses position `(3, 3, 5)`, target `(0, 0, 0)`, a 60-degree vertical FOV and clipping planes at 0.1 and 100. `app.camera()` accesses the current camera; creating another Camera makes that camera active without invalidating earlier references.

Load a model through the same Object interface:

```cpp
auto& bunny = app.create<Jade::Object>();
bunny.setMesh(Jade::Mesh::load("assets/bunny.obj"));
bunny.setScale({16, 16, 16});
bunny.setPos({1.9f, -1.7f, 0});
```

The bundled bunny has 453 vertices and 948 triangles. CMake copies `assets/` beside the Demo executable on every build, including when only an asset changes. Paths passed to `Mesh::load()` are relative to the working directory; `"bunny.obj"` works when that file is directly in that directory. Source coordinates and connectivity are preserved; the Demo scales and positions the Object. See [asset provenance and terms](assets/README.md).

`Vertex` contains `position` and `normal` as Vec3 values. `Mesh` exposes `std::vector<Vertex> vertices` and `std::vector<std::uint32_t> indices`; every three indices form one triangle. Cube uses 24 vertices and 36 indices to keep a separate normal for each face. `Object::setMesh()` takes an owned snapshot, validates finite data and indices, and preserves the previous mesh on invalid input. An empty mesh draws nothing. `GetMesh()` provides const access; replace a mesh with `setMesh()` to update it.

The OBJ loader supports positive and negative indices, `v`, `v/vt`, `v//vn` and `v/vt/vn` faces, triangles and convex polygons. It preserves explicit normals and generates area-weighted smooth normals when absent. UVs, materials and smoothing-group boundaries are not retained. Concave polygons should be triangulated before loading. File and parse failures include the path and line number in a `std::runtime_error`.

Perspective makes distant objects appear smaller. Orthographic projection preserves their size regardless of distance. FOV is a perspective setting, expressed in degrees; orthographic projection uses a visible height in world units:

```cpp
camera.setPerspective(60.0f, 0.1f, 100.0f);
camera.setOrthographic(6.0f, 0.1f, 100.0f);
```

The camera uses a right-handed world with Y up. `setPos()` translates the eye and target together, preserving orientation; `lookAt()` explicitly aims at a target. `getPos()`, `getFront()`, `getRight()` and `getUp()` support camera controls; direction getters return unit vectors including its rotation offset. Target and position must differ, and up must not be parallel to the viewing direction. FOV must be strictly between 0 and 180; visible height and near/far distances must be positive, with far greater than near. Invalid updates throw without replacing the previous state.

Object owns a mesh, color and transform: `setPos(Vec3)`, `setRotation(Vec3)` in degrees and `setScale(Vec3)`. Scale and builtin cube size must be positive. Object rotation applies X, then Y, then Z, after scaling and before translation. `setColor(Color)` sets the base color; normals control ambient and directional diffuse lighting, including after rotation and nonuniform scaling. Camera inherits Object; its rotation is a local offset from its look-at orientation and `lookAt()` resets that offset. Camera scale is inverted in the view transform, with `(1, 1, 1)` as the default.

`app.setWireframe(true)` draws the triangle edges of Object meshes and Triangle; `app.setWireframe(false)` restores filled faces. The setting takes effect at the next frame and defaults to false. Cube faces contain two triangles, so wireframe also shows their diagonals. Line remains a line in both modes.

The Axis Gizmo draws red +X, green +Y and blue +Z segments from the world origin, each exactly one world unit long. It follows the active camera projection and is drawn over 3D objects so the origin remains visible. It is enabled by default; bare F3 toggles once per press and logs the state. Use `config.axisGizmo = false`, `app.setAxisGizmo(bool)` or `app.axisGizmoEnabled()` for application control. Consuming the F3 press prevents its default toggle.

Resizing updates the viewport and projection from the actual framebuffer size. Perspective keeps vertical FOV fixed, and orthographic keeps visible height fixed; both preserve horizontal/vertical scale.

## Vectors and 2D drawing

`Vector.hpp` provides Vec2 and Vec3, with `+`, `-`, unary `-`, scalar multiplication in either order, and `dot()`/`cross()` as members or free functions. A 2D cross product returns a signed scalar; a 3D cross product returns a perpendicular vector. Vec2D and Vec3D remain aliases.

```cpp
Jade::Vec3 a{1, 0, 0};
Jade::Vec3 b{0, 1, 0};
auto sum = a + b;
auto scaled = a * 2.0f;
auto alignment = Jade::dot(a, b); // 0
auto normal = Jade::cross(a, b);  // (0, 0, 1)

auto& line = app.create<Jade::Line>();
line.setPos({1, 1}, {4, 5});
auto& triangle = app.create<Jade::Triangle>(
  Jade::Vec2{1, 1}, Jade::Vec2{5, 1}, Jade::Vec2{1, 5});
triangle.setColor({1, 0, 0, 1});
```

Lines and filled Triangles use 2D drawing units with `(0, 0)` at the lower-left corner. `config.viewHeight` defaults to 10; visible width follows the framebuffer aspect ratio. Lines remain white and one pixel wide. Each Line stores exactly two copied endpoints; `setPos(start, end)` changes them atomically, and `Start()`/`End()` provide read-only access. Triangle owns exactly three vertices. Non-finite positions are rejected, and degenerate primitives draw nothing.

The frame draws depth-tested Cubes first, then Triangles and Lines as 2D overlays. The 3D camera affects Cubes; the overlay retains its existing coordinate system. Created objects and cameras belong to Application, and returned references remain valid until destruction. Creation, mutation, window operations and `Run()` belong to the main thread.

## Keyboard, mouse and frame events

Register events before `Run()` or from another callback. Callbacks accept no arguments or the event by const reference:

```cpp
app.on<Jade::KeyHeld<"w">>([&](const auto& event) {
  Jade::Motion::forward(camera, 3.0f * event.dt);
});
app.on<Jade::KeyPressed<"C-S-Space">>([&] { app.setWireframe(true); });
auto subscription = app.on<Jade::KeyReleased<"Space">>([] { /* released */ });
subscription.disconnect();
app.on<Jade::FrameUpdate>([](const Jade::FrameUpdate& event) {
  // event.dt is seconds; event.frame starts at 1 for each Run().
});
bool dragging = false;
app.on<Jade::MousePressed<"Left">>([&] { dragging = true; });
app.on<Jade::MouseReleased<"Left">>([&] { dragging = false; });
app.on<Jade::MouseMoved>([&](const auto& mouse) {
  if (dragging) {
    Jade::Motion::yaw(camera, mouse.dx * 0.1);
    Jade::Motion::pitch(camera, -mouse.dy * 0.1);
  }
});
app.on<Jade::MouseScrolled>([&](const auto& mouse) {
  Jade::Motion::zoom(camera, mouse.dy);
});
```

`KeyPressed` and `KeyReleased` fire once per transition, including quick taps between frames. `KeyHeld` fires once per rendered frame, independently of OS key repeat. `FrameUpdate` runs after held keys and before rendering. First-frame dt is zero; subsequent values measure actual time between frame starts. Use units per second times dt for movement independent of frameLimits.

Key names are case-insensitive. Space, Up, Down, Left and Right are supported alongside letters, digits, F1-F12 and common navigation/modifier keys. Prefixes C-, S-, A- and M- mean Control, Shift, Alt and Super. Modifier matching is exact: `w`, `C-w` and `C-S-w` are separate bindings. Caps/Num Lock do not affect matching. Invalid names fail at compile time; the string-template syntax requires C++20.

`MousePressed`, `MouseReleased` and `MouseHeld` use Left, Right, Middle or Button4-Button8, case-insensitively. Pressed/Released fire once per transition; Held runs each frame with `event.dt`, as do held keys. Button bindings match with any keyboard modifiers, so releasing after a modifier change still ends a drag. `MouseScrolled` carries horizontal/vertical `dx`/`dy` offsets, including fractional scrolling. Pixel and scroll deltas already represent displacement; do not multiply them by dt.

Callbacks run on the main thread in registration order. Returning `Jade::EventFlow::Stop` stops later callbacks for that event; consuming Escape also prevents its default close action. `app.stop()` ends the current Run and allows a later Run to resume. Ignoring an EventConnection leaves it registered; explicitly disconnect to cancel it. Focus loss releases held keys and mouse buttons. See [Events](docs/Events.md) for dispatch, cancellation and lifetime rules.

Motion provides `forward`, `backward`, `right`, `left`, `up`, `down`, `yaw`, `pitch`, `roll` and `zoom`, each explicitly taking a Camera. Movement defaults to 0.1 world units per call; up/down use world Y while forward/right use the camera's current axes. Rotation increments use degrees relative to its look-at frame: positive yaw turns right, positive pitch looks up, and positive roll banks clockwise. Yaw/roll wrap and pitch is limited to [-89, 89]. MouseMoved contains window coordinates x/y and deltas dx/dy; Y grows downward, so the demo negates dy for pitch. The first cursor sample after focus has zero delta. Zoom uses positive scroll to magnify and negative scroll to shrink, changing perspective FOV or orthographic view height while retaining camera position, orientation and clip planes. Demo rotates only during a left-button drag and zooms with the wheel.

Press O in Demo to switch between Perspective and Orthographic. Switching calls `camera.setProjection(...)`, keeps each mode's zoom settings and the current pose/clip planes, and logs the selected mode. Holding O does not repeatedly toggle it.

## Public headers

Public declarations live in independent headers under `Jade/include/Jade/`. Include the interfaces you use directly, or use `Jade.hpp` as an optional entry point that only includes them:

```cpp
#include <Jade/Application.hpp>
#include <Jade/Objects/Camera.hpp>
#include <Jade/Objects/Object.hpp>
#include <Jade/Mesh/Mesh.hpp>
#include <Jade/Geometry/Line.hpp>
#include <Jade/Geometry/Triangle.hpp>
#include <Jade/Vector.hpp>
```

Geometry contains the screen-space 2D Line and Triangle headers; neither inherits Object. Objects contains Object, Camera and Motion for the 3D world. Mesh/Mesh.hpp and Mesh/Vertex.hpp define CPU mesh data; cube is a builtin mesh rather than an Object subclass. Input/Key.hpp and Input/MouseButton.hpp define key/button names and modifiers; Events/Event.hpp, Events/Keyboard.hpp, Events/Mouse.hpp, Events/MouseMoved.hpp, Events/MouseScrolled.hpp and Events/FrameUpdate.hpp define connections, propagation and event types. The remaining root headers are Color.hpp, Log.hpp and WindowConfig.hpp. ApplicationConfig and RunOptions live with Application. Window, Renderer, GLFW/OpenGL backends, OBJ parsing, input state, event dispatch, matrix calculations, validation helpers and FrameClock are private under `Jade/src/internal/`; consumer include paths do not expose them.

## Frame scheduling and logs

`config.frameLimits` defaults to `24` and controls frames per second. The main loop uses monotonic absolute deadlines and waits for events between frames. Slow frames skip missed slots instead of causing catch-up bursts. VSync defaults to `false`; enabling it can further limit the achieved rate. A zero frame rate or a non-positive/non-finite view height is rejected.

Every submitted frame emits a log such as:

```text
[Jade] elapsed=1.044s second=2 frame=26 frame_in_second=2 fps=24.0 frame_limits=24
```

`elapsed` is seconds since `Run()` started. `second` is a one-based elapsed-second bucket; `frame` is the total submitted frame count and `frame_in_second` resets when the bucket changes. `fps` is measured from completion intervals in the most recent second, with `0.0` for the first frame. `frame_limits` is the configured scheduling rate.

Set `config.logFrames = false` to disable frame logs. Minimized windows skip drawing and frame logs. Each `Run()` resets frame statistics and keeps created objects and cameras. `RunOptions::frameLimit` controls the total number of frames in a bounded run, independently of `config.frameLimits`; `validateRendering` checks clear pixels before any objects are drawn.

## Dependencies and runtime

[FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html) downloads GLFW 3.4, GLAD 2.0.8 and, when tests are enabled, GoogleTest 1.17.0. Every archive has a SHA-256 check. Dependency sources stay under each build directory's `_deps/`. All three libraries link statically. GLFW examples, tests, documentation and installation are disabled.

[GLAD's official CMake interface](https://github.com/Dav1dde/glad/blob/v2.0.8/cmake/GladConfig.cmake) generates OpenGL 3.3 Core with `REPRODUCIBLE` and `EXTENSIONS NONE`. Generated files stay under `build/<configuration>/generated/glad/`. The generator uses a separate `python/venv/` with Jinja2 3.1.6 and MarkupSafe 3.0.3. Download, environment creation and package-installation failures include the command output.

The engine, dependencies and consumers share the MSVC dynamic CRT: `/MDd` for Debug and `/MD` for Release. GoogleTest follows the [official shared-CRT configuration](https://google.github.io/googletest/quickstart-cmake.html).

Tests exercise public object and camera APIs; rendering tests additionally use internal Window/Renderer and OpenGL/GLFW to inspect pixels and resize hidden windows. Rendering checks require an OpenGL 3.3-capable runtime. They do not require a particular Visual Studio installation path, Mesa installation, Windows message-injection script or preinstalled GoogleTest package. A graphics driver can provide OpenGL; Windows machines without one can optionally use the local Mesa DLLs described in [runtime/README.md](runtime/README.md).

Mesa files are ignored under `runtime/mesa/x64/`. On first configuration, detecting `opengl32.dll` enables `JADE_USE_SOFTWARE_RENDERING`; `libgallium_wgl.dll` must also be present. `JADE_MESA_RUNTIME_DIR` can point to another directory.

```powershell
# The next build removes deployed Mesa DLLs when software rendering is disabled.
cmake --preset windows-debug -DJADE_USE_SOFTWARE_RENDERING=OFF
cmake --build --preset windows-debug

cmake --preset windows-debug -DJADE_USE_SOFTWARE_RENDERING=ON
cmake --build --preset windows-debug
& ./build/windows-debug/bin/JadeSmokeTests.exe
```

With software rendering enabled, Jade automatically selects llvmpipe before initializing GLFW. Direct execution and Visual Studio's Ninja presets need no environment setup. An existing non-empty `GALLIUM_DRIVER` value takes precedence; disabling software rendering leaves it untouched. See the runtime documentation for details.

Demo and tests share the same deployment rule. Disabling Mesa uses the system graphics driver. Original DLLs in `runtime/` remain untouched.

## Layout and tests

```text
Jade/
|-- CMakeLists.txt             # Project and subdirectories
|-- CMakePresets.json          # Windows x64 Debug / Release
|-- Jade/                     # Engine static library
|   |-- CMakeLists.txt
|   |-- include/Jade/         # Core, Geometry/, Objects/, Mesh/, Input/, Events/ and Jade.hpp
|   `-- src/                  # Core, Geometry, Objects, Mesh/builtins and internal backends
|-- Demo/                     # Example application
|-- assets/                   # Bunny OBJ and source attribution
|-- tests/                    # GoogleTest data, scheduling and rendering checks
|-- cmake/                    # Dependencies, build options and deployment
|-- runtime/mesa/x64/          # Ignored optional Mesa DLLs
|-- docs/                     # Architecture and validation records
`-- build/                    # Downloads, generated files and build outputs
```

Engine sources are listed explicitly in CMake. Project headers use `.hpp` and implementations use `.cpp`; every implementation includes its matching header first. Public-facing implementations live under Core, Geometry, Objects and Mesh, with builtin generation under Mesh/builtins. Platform and rendering facades/backends, OBJ parsing, FrameClock, matrices and validation live under `src/internal/`. Applications receive only the public include directory.

Sixty-three GoogleTests cover vector operations, transforms and projection, camera/geometry validation, mesh ownership, cube winding/normals, OBJ indices and errors, bundled bunny loading, frame deadlines/logs, Line/Triangle pixels, mesh resizing, depth, clipping, lighting and wireframe. Axis checks cover unit world length, both projections, resize, camera motion, drawing over objects and F3 propagation. Input tests cover quick taps, repeat suppression, focus loss, exact modifier chords, mixed mouse/key arrival order, callback ordering/cancellation, Motion movement/rotation and stopping/resuming the loop. The original three-clear-frame smoke check remains. Tests build independently of Demo.

See [Architecture](docs/Architecture.md) for module boundaries and [Validation](docs/Validation.md) for recorded results.
