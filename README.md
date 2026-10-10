# Jade

Jade is a C++20 static engine library built with CMake, currently validated on Windows x64 / MSVC. World owns entities and components, Assets owns shared immutable meshes, and an internal RenderSystem draws 3D geometry through OpenGL 3.3 Core. It includes quaternion camera controls, perspective/orthographic projection, OBJ loading, depth, wireframe, unit world axes, typed input events and a frame-limited loop. Include specific public headers or `<Jade/Jade.hpp>` and link `Jade::Jade`.

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

## Entity and Mesh demo

Demo displays a cube and the smallest bundled Stanford Bunny at 24 FPS. WASD/arrows move the camera, Space/Ctrl+Space move along world Y, Shift+W is faster and Ctrl+W slower. Left-button dragging turns the camera, the wheel zooms, Q/E roll, O switches projection, F toggles wireframe, F3 toggles axes and Esc exits.

```cpp
#include <Jade/Jade.hpp>

int main()
{
  Jade::ApplicationConfig config;
  config.frameLimits = 24;
  Jade::Application app(config);

  const auto mesh = app.assets().add(Jade::Mesh::cube(2.0f));
  auto cube = app.world().create();
  cube.add<Jade::Transform>();
  cube.add<Jade::MeshRenderer>(mesh);

  app.Run();
}
```

An Entity is an identity, with no implicit components or behavior. World owns its component data. Components are ordinary C++ types; no inheritance is required:

```cpp
struct Velocity { Jade::Vec3 value; };

auto entity = app.world().create();
entity.add<Jade::Transform>();
entity.add<Velocity>(Jade::Vec3{1, 0, 0});
app.world().each<Jade::Transform, Velocity>(
  [](Jade::Entity, Jade::Transform& pose, Velocity& velocity) {
    pose.position = pose.position + velocity.value * 0.01f;
  });
```

`add<T>(args...)` constructs a component and returns its reference. `get<T>()`, `has<T>()` and `remove<T>()` access state; duplicate add throws `std::logic_error`, missing get throws `std::out_of_range`, and removing an absent component is a no-op. `world.destroy(entity)` removes the identity and its components. Generation IDs prevent old handles from accessing reused slots. `valid()` becomes false after entity or World destruction. Component references survive unrelated creation, but removal and owner destruction invalidate them. A const Entity handle still accesses mutable components.

`world.each<T...>()` snapshots matching identities before callbacks. New entities or new matches wait until the next query; destroyed entities and removed matches are skipped. When removing a component during a callback, stop using its reference. Scene mutation and rendering use the main thread.

## Assets and world geometry

A Mesh is a resource, shared by handles. Each entity supplies its own Transform and MeshRenderer color:

```cpp
const auto bunnyMesh = app.assets().load<Jade::Mesh>("assets/bunny.obj");
auto bunny = app.world().create();
auto& pose = bunny.add<Jade::Transform>();
pose.scale = {16, 16, 16};
pose.position = {1.9f, -1.7f, 0};
bunny.add<Jade::MeshRenderer>(bunnyMesh, Jade::Color{1, 0.8f, 0.55f, 1});

// Another entity can use the same asset without copying its vertex data.
auto copy = app.world().create();
copy.add<Jade::Transform>().position = {-2, 0, 0};
copy.add<Jade::MeshRenderer>(bunnyMesh);
```

`assets.add<T>(resource)` stores an immutable CPU snapshot. `assets.get(handle)` returns `const T&`; `assets.load<Mesh>(path)` synchronously loads OBJ. Canonical absolute paths share a cache entry. Relative paths use the working directory. Assets remain alive until the owner is destroyed; handles do not extend that lifetime. Empty, expired or foreign handles are rejected by get. There is no unload, hot reload or Material in this version. An empty MeshRenderer handle disables its drawing.

The bundled bunny has 453 vertices and 948 triangles. CMake copies `assets/` beside Demo on every build; launching from the repository root or executable's `bin` directory finds `assets/bunny.obj`. Source geometry stays intact while Transform scales and positions it. See [asset provenance and terms](assets/README.md).

Mesh contains `vertices` (Vertex position/normal), `indices` (uint32) and `topology`. Every two indices form a line for `MeshTopology::Lines`; every three form a triangle for `Triangles`, the default. Registration validates finite vertex data, index bounds and complete primitives. An empty mesh draws nothing. Builders produce local 3D geometry:

```cpp
auto lineMesh = app.assets().add(Jade::Mesh::line({1, 1, 0}, {4, 5, 0}));
auto line = app.world().create();
line.add<Jade::Transform>();
line.add<Jade::MeshRenderer>(lineMesh);
line.get<Jade::Transform>().position = {0, 0, -1};

const auto triangleMesh = app.assets().add(
  Jade::Mesh::triangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0}));
```

All meshes use the active camera and world depth. Lines use unlit color; triangles use normal-based ambient and directional lighting. `Mesh::cube(size)` is centered at the local origin, with 24 vertices and 36 indices for hard face normals. Triangles generate a face normal. Degenerate primitives draw no area. The former Object class, Geometry Line/Triangle classes, `Application::create<T>()`, `Mesh::load()` and fixed 2D overlay have been removed.

OBJ supports positive/negative indices, `v`, `v/vt`, `v//vn` and `v/vt/vn` faces, triangles and convex polygons. Explicit normals are normalized; absent normals receive area-weighted smoothing. UVs, materials and smoothing-group boundaries are not retained. Triangulate concave polygons before loading. File/parse errors report the path and line in `std::runtime_error`.

## Camera and transforms

The default camera Entity contains Transform at `(3, 3, 5)` aimed at the origin and Camera with perspective FOV 60 degrees, orthographic height 6, near 0.1 and far 100. Adding another Camera component does not activate it; select it explicitly:

```cpp
auto camera = app.camera();
auto& pose = camera.get<Jade::Transform>();
auto& lens = camera.get<Jade::Camera>();
pose.position = {4, 3, 8};
Jade::Motion::lookAt(pose, {0, 0, 0});
lens.projection = Jade::Projection::Orthographic;
lens.viewHeight = 6;

// app.setCamera(otherCameraEntity); // Same World; Transform + Camera required.
```

Transform contains position, quaternion rotation and scale. The right-handed world has Y up; an identity rotation faces local -Z with right +X and up +Y. Model transforms apply scale, rotation, then translation. Camera view ignores scale. Moving position keeps orientation; `Motion::lookAt()` replaces rotation once and retains no target. Quaternion angles use degrees through `Quaternion::angleAxis(axis, degrees)`. Motion yaw/pitch/roll compose about the current local axes, permit complete flips and normalize the result. Positive yaw turns right, pitch looks up and roll banks clockwise. This also applies after Q/E roll.

Perspective makes distant geometry smaller; orthographic keeps its size. FOV controls perspective vertical coverage, and viewHeight controls orthographic vertical coverage. Resize adjusts horizontal coverage from the actual framebuffer aspect ratio. Both lens values must stay valid, even when inactive. Positions/quaternions must be finite, rotations nonzero, scale positive, color in [0, 1], FOV strictly (0, 180), heights/clipping distances positive and far greater than near. Since components are plain data, consumers validate them when used. Invalid render data throws from Run.

`app.setCamera()` rejects foreign/dead entities and missing components. If the selected camera is later destroyed or loses Transform/Camera, Run keeps processing input and clearing/presenting without world or axis drawing. Selecting a valid camera resumes rendering.

## Events and controls

```cpp
auto camera = app.camera();
app.on<Jade::KeyHeld<"w">>([camera](const auto& event) {
  if (camera.has<Jade::Transform>()) {
    Jade::Motion::forward(camera.get<Jade::Transform>(), 3.0f * event.dt);
  }
});
app.on<Jade::MouseScrolled>([camera](const auto& mouse) {
  if (camera.has<Jade::Camera>()) {
    Jade::Motion::zoom(camera.get<Jade::Camera>(), mouse.dy);
  }
});
```

Pressed/Released fire once per transition, including taps between frames. Held fires each submitted frame independently of OS repeat. FrameUpdate follows held keys/buttons, before rendering. First-frame dt is zero; later values are seconds between frame starts. Multiply movement speed by dt. Mouse pixel and wheel deltas already describe displacement; do not multiply by dt.

Keys are case-insensitive, including Space, arrows, letters, digits, F1-F12 and navigation/modifier keys. C-, S-, A-, M- denote Control, Shift, Alt, Super; matching is exact. Mouse buttons include Left, Right, Middle and Button4-Button8 and match regardless of keyboard modifiers. Invalid bindings fail compilation. Focus loss releases keys/buttons and resets cursor deltas. Callbacks run on the main thread in registration order. Returning EventFlow::Stop stops propagation; EventConnection::disconnect() cancels a subscription. `app.stop()` permits a later Run. See [Events](docs/Events.md) for details and dragging examples.

Motion translation/rotation takes Transform; zoom takes Camera. Movement defaults to 0.1 units per call. Forward/right follow local axes; up/down follow world Y. Positive wheel offsets magnify the active lens parameter, keeping pose and clipping planes intact. O changes `lens.projection`, retaining both modes' zoom values.

The `Events` Logger records subscription/cancellation and key/button edges at Debug.
Dispatch records include canonical bindings, the number of handlers actually called
and the consuming subscription ID (`consumed_by=0` means continued propagation).
Held, cursor, scroll and FrameUpdate details use Trace. Callback failures use Error
with the subscription and original exception message, then rethrow unchanged.
Explicit cancellation is logged when the next connection/dispatch prunes it.
Events follows the global logging threshold independently of `logPerformance`.

## Public headers

Public interfaces are split by purpose, with one optional includes-only entry point, Jade.hpp:

| Folder/header | Interface |
| --- | --- |
| Core/ | World, Entity, Transform |
| Math/ | Vector (Vec2/Vec3), Quaternion |
| Graphics/ | Mesh, Vertex, MeshRenderer, Camera, Motion |
| Assets/ | Assets, AssetHandle and MeshHandle |
| Input/, Events/ | Key/button bindings and event contracts |
| Application.hpp, Color.hpp, Log.hpp, WindowConfig.hpp | Application loop and value/configuration types |

There are no folder aggregation headers. Source `.cpp` files include their matching private `.hpp` first and are explicitly listed in CMake. Registry storage, RenderSystem, backend, OBJ parsing, input state, event dispatch, matrices and clock are internal. Consumers receive only `Jade/include`.

## Frame scheduling and logs

`config.frameLimits` defaults to 24 frames per second and must be positive. The loop uses monotonic absolute deadlines and waits for events between frames; slow frames skip missed slots. VSync defaults to false and may further constrain the achieved rate. `app.setWireframe(bool)` changes triangle rasterization; unit world axes default on and draw over meshes without writing depth.

```text
15:27:07.591522  INFO [Application] - Ready: window=960x640 target_fps=24 vsync=off
15:27:07.592123 DEBUG [Assets] - Loaded mesh: path=assets/bunny.obj vertices=453 triangles=948 duration=1.20ms
15:27:07.593456  INFO [Application] - Run started
15:27:08.635123 DEBUG [Performance] - fps=24.0 interval_avg=41.67ms interval_max=44.31ms work_avg=2.10ms work_max=5.30ms
```

Application records readiness, Run start/stop and exception aborts; Assets records
successful file loads at Debug and failures at Error while preserving exceptions.
Cache hits do not repeat successful load messages. Axis Gizmo, camera projection
and Demo wireframe changes are event logs. Each Run resets timing and retains World,
Assets and subscriptions. `RunOptions::frameLimit` limits total submitted frames
independently of FPS; `validateRendering` checks the clear before drawing.

`ApplicationConfig::logPerformance` defaults to true. A completed one-second
measurement window emits one Debug summary with FPS, average/maximum frame interval
and average/maximum work time. Interval includes frame pacing; work measures wall
time from held/update callbacks through presentation, excluding the explicit
frame-pacing wait. Work can include blocking presentation and is not a GPU timer.
The first frame warms up measurement; no zero-FPS or partial final summary is emitted.
Minimized windows reset the measurement window so suspended time is not a hitch.

A frame interval exceeding both 100 ms and three target frame periods emits a Warn
hitch message, limited to one per second. Small scheduling variations near the cap
are not warnings. Trace verbosity additionally enables per-frame index/interval/work
messages. Set `config.logPerformance = false` to disable all performance diagnostics;
use `Log::SetLevel(Log::Level::Info)` to keep lifecycle/resource errors and hide
Debug summaries, or `Level::Trace` to explicitly inspect individual frames.
The old `logFrames` field and per-frame Info counters are removed.

This follows the event-oriented output described in the
[Unity Console documentation](https://docs.unity3d.com/6000.0/Documentation/Manual/Console.html)
and the optional once-per-second FPS output in
[Godot's logging documentation](https://docs.godotengine.org/en/stable/tutorials/scripting/logging.html).

`Jade::Log` defaults to synchronous terminal output at Debug and above. Trace, Debug,
Info, Warn, Error and Fatal share process-wide output configuration, independent of Application
lifetime. Error/Fatal go to stderr; other levels go to stdout. Every line includes
local time with six fractional digits (microseconds), a right-aligned five-character
severity and the Logger category. Fatal logs without terminating the process.
Supported terminals receive a gray timestamp and severity colors (dim Trace,
cyan Debug, green Info, yellow Warn, red Error, bold red Fatal). Windows console
ANSI processing is enabled when supported. Redirected streams and log files stay
plain text; a nonempty `NO_COLOR` or `TERM=dumb` also disables console colors.

```cpp
#include <Jade/Log.hpp>

Jade::Log::Config logging;
logging.minimumLevel = Jade::Log::Level::Debug;
logging.file = "jade.log"; // Append; parent directories must already exist.
Jade::Log::Configure(logging);

const Jade::Log::Logger logger{"Assets"};
logger << Jade::Log::info("Loaded {} vertices", 453);
logger << Jade::Log::warn("Missing mesh: {}", "bunny.obj");
Jade::Log::SetLevel(Jade::Log::Level::Warn);
Jade::Log::Flush();
```

Each Logger owns its category name. The lowercase `trace`, `debug`, `info`, `warn`,
`error` and `fatal` functions create owning, move-only Entries; `logger << entry`
consumes and writes one entry. Chaining is supported. Creation and destruction
never write logs, and moved-from or consumed entries cannot emit again. There is
no default main logger, standalone automatic output, public Write function or
compatibility API.

Messages with arguments use C++20 `std::format` syntax and compile-time format
checks. `info(message)` and the other single-message helpers accept runtime
strings and preserve literal braces. Entries format immediately when created,
skipping disabled levels; insertion checks the current threshold again. A filtered
entry cannot become active later. C++ still evaluates arguments before the helper
call; use `Enabled` before computing expensive arguments. `Level::Off` disables
logging. Performance summaries use Debug, hitch warnings use Warn, and individual
frames use Trace; all obey `logPerformance` and the logger's minimum level.

```cpp
const Jade::Log::Logger renderer{"Renderer"};
renderer << Jade::Log::debug("FPS: {:.1f}", 60.0)
         << Jade::Log::info("Ready");

auto entry = Jade::Log::warn("Delayed message");
renderer << std::move(entry); // Explicitly emit a stored entry once.
```

`Config::console`, `file` and `sink` independently control the destinations. A sink
receives a `const Record&` containing an owning category/message and timestamp; copy
it to retain it. Callbacks run synchronously outside the output lock, can run
concurrently, and must synchronize shared state. Nested logging writes to the
console/file without invoking the sink again. Replacing a sink allows callbacks
already in progress to finish; stop producers before destroying their captured
state. File/console writes are serialized, while callback order across threads is
unspecified. `Flush` flushes built-in outputs; Error/Fatal also flush the file.

`Configure()` restores defaults and closes the previous file. A failed file open
throws and preserves the existing configuration. Formatting, output and sink
failures during logging are reported to stderr without escaping into engine code.
There are no logging dependencies, worker threads or automatic configuration files.

## Dependencies and runtime

[FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html) downloads GLFW 3.4, GLAD 2.0.8 and, when tests are enabled, GoogleTest 1.17.0. Every archive has a SHA-256 check. Dependency sources stay under each build directory's `_deps/`. All three libraries link statically. GLFW examples, tests, documentation and installation are disabled.

[GLAD's official CMake interface](https://github.com/Dav1dde/glad/blob/v2.0.8/cmake/GladConfig.cmake) generates OpenGL 3.3 Core with `REPRODUCIBLE` and `EXTENSIONS NONE`. Generated files stay under `build/<configuration>/generated/glad/`. The generator uses a separate `python/venv/` with Jinja2 3.1.6 and MarkupSafe 3.0.3. Download, environment creation and package-installation failures include the command output.

The engine, dependencies and consumers share the MSVC dynamic CRT: `/MDd` for Debug and `/MD` for Release. GoogleTest follows the [official shared-CRT configuration](https://google.github.io/googletest/quickstart-cmake.html).

Tests exercise public World, Assets and component APIs; rendering tests additionally use internal Window/Renderer and OpenGL/GLFW to inspect pixels and resize hidden windows. Rendering checks require an OpenGL 3.3-capable runtime. They do not require a particular Visual Studio installation path, Mesa installation, Windows message-injection script or preinstalled GoogleTest package. A graphics driver can provide OpenGL; Windows machines without one can optionally use the local Mesa DLLs described in [runtime/README.md](runtime/README.md).

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
Jade/include/Jade/   # Core/, Math/, Graphics/, Assets/, Input/, Events/
Jade/src/Core/       # Application, Entity, World, Log
Jade/src/Math/       # Quaternion
Jade/src/Graphics/   # Mesh, Motion, builtins/Cube
Jade/src/Assets/     # Shared resource storage
Jade/src/internal/   # Registry, RenderSystem, OpenGL, platform, loader, input, math
Demo/               # Entity-based cube and bunny example
assets/             # Bunny OBJ and attribution
tests/              # ECS, assets, math, input, scheduling and pixel checks
```

GoogleTest exercises identity generations, ownership, component/query mutation, shared immutable resources, OBJ loading, quaternion controls, projections, resize, depth, lighting, wireframe, axes and events using real OpenGL readback. Tests build independently of Demo. See [Architecture](docs/Architecture.md) for boundaries and [Validation](docs/Validation.md) for recorded checks.
