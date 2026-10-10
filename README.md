# Jade

Jade is a C++20 static engine library validated on Windows x64 / MSVC. Its rendering kernel uses CPU progressive path tracing with shared triangle meshes, two-level BVHs, diffuse/metal/dielectric BSDFs and emissive geometry. OpenGL 3.3 presents the completed image and draws unlit wireframe/axis diagnostics. Scene owns entities, hierarchy, events and camera selection; Assets owns immutable resources. Include individual public headers or `<Jade/Jade.hpp>` and link `Jade::Jade`.

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

## Scenes and entities

Demo starts with a diffuse Stanford Bunny, a rough metal cube, a glass sphere and an emissive ceiling mesh. Press 1 for this scene or 2 for an animated three-level sphere hierarchy with an emissive sun; P pauses that animation. The input/presentation loop targets 24 FPS, independently of path-tracing sample throughput. Each scene retains its own camera and controls. WASD/arrows move the camera, Space/Ctrl+Space move along parent Y, Shift+W is faster and Ctrl+W slower. Left-button dragging turns the camera, the wheel zooms, Q/E roll, O switches projection, F switches between path tracing and unlit wireframe, F3 toggles axes and Esc exits.

```cpp
#include <Jade/Jade.hpp>

int main()
{
  Jade::ApplicationConfig config;
  config.debug = true; // Enable Application F/F3/O shortcuts.
  Jade::Application app(config);
  app.use<Jade::Renderer::RayTracing>();
  auto scene = std::make_shared<Jade::Scene>();

  auto camera = scene->create();
  auto& pose = camera.add<Jade::Transform>();
  pose.position = {4, 3, 8};
  Jade::Motion::lookAt(pose, {0, 0, 0});
  camera.add<Jade::Camera>();
  scene->setCamera(camera);
  scene->use<Jade::Module::CameraControl>();

  const auto mesh = app.assets().add(Jade::Mesh::cube(2.0f));
  auto cube = scene->create();
  cube.add<Jade::Transform>();
  cube.add<Jade::Geometry>(mesh);
  cube.add<Jade::Material>();

  auto lamp = scene->create();
  auto& lampPose = lamp.add<Jade::Transform>();
  lampPose.position = {0, 4, 0};
  lampPose.scale = {1.5f, 0.05f, 1.5f};
  lamp.add<Jade::Geometry>(mesh);
  auto& emission = lamp.add<Jade::Material>();
  emission.baseColor = {0, 0, 0};
  emission.emission = {14, 12, 10};

  app.setScene(scene);
  app.Run();
}
```

Application starts without a scene; a new Scene is empty, requires no window and creates no implicit camera. Assets remain shared at Application scope. `app.scene()` returns the active `shared_ptr<Scene>`; `app.setScene(nullptr)` unloads it and leaves the application clearing/presenting and dispatching global events.

Components are ordinary C++ types; no inheritance is required:

```cpp
struct Velocity { Jade::Vec3 value; };

auto entity = scene->create();
entity.add<Jade::Transform>();
entity.add<Velocity>(Jade::Vec3{1, 0, 0});
for (auto entity : scene->view<Jade::Transform, Velocity>()) {
  auto& pose = entity.get<Jade::Transform>();
  pose.position = pose.position + entity.get<Velocity>().value * 0.01f;
}
```

`add<T>(args...)` constructs a component and returns its reference. `get<T>()`, `has<T>()` and `remove<T>()` access state; duplicate add throws `std::logic_error`, missing get throws `std::out_of_range`, and removing an absent component is a no-op. `scene->destroy(entity)` destroys the entity and its descendants. All subtree handles become invalid before component destructors run; components are released children before parents. Generation IDs prevent old handles from accessing reused slots. Handles and views do not extend Scene lifetime. Component references survive unrelated creation, but removal and owner destruction invalidate them. A const Entity handle still accesses mutable components.

`scene->view<T...>()` snapshots matching identities. New entities or new matches wait until the next query; destroyed entities and removed matches are skipped when advancing the iterator. Keep a named view alive while using its iterators. Removing/replacing a component invalidates its references immediately. Scene mutation and rendering use the main thread.

```cpp
auto root = scene->create(); // An identity group needs no Transform.
auto child = scene->create();
child.add<Jade::Transform>().position = {2, 0, 0};
scene->setParent(child, root);
auto matrix = scene->worldMatrix(child); // Jade::Matrix4, column-major.
scene->setParent(child);                 // Detach, retaining local TRS.
```

Hierarchy belongs to Scene; Transform contains local position, rotation and scale. `parent(entity)` returns an empty Entity for a root; `children(entity)` returns an attachment-order snapshot. Self-parenting, cycles and foreign/dead entities are rejected before changing links. Removing Transform preserves links and makes that entity an identity group. Reparenting retains local TRS, so world pose can change. Full parent × local matrices preserve shear from nonuniform scale and rotation.

The World type/header, `Application::world()/camera()/setCamera()` and callback-based `each()` are removed. Use Scene directly; there are no compatibility aliases.

## Meshes, instances and materials

Every physical object is a triangle Mesh instance. Geometry references an immutable mesh resource; Material describes scattering and emission independently:

```cpp
const auto bunnyMesh = app.assets().load<Jade::Mesh>("assets/bunny.obj");
auto bunny = scene->create();
bunny.add<Jade::Transform>().scale = {16, 16, 16};
bunny.add<Jade::Geometry>(bunnyMesh);
bunny.add<Jade::Material>().baseColor = {0.8f, 0.28f, 0.12f};

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
lamp.emission = {14, 12, 10}; // Linear HDR radiance, allowed to exceed 1.
```

All illumination comes from emission on actual geometry. There are no point/directional light entities, ambient term or background illumination. Escaped rays return black. A scene without emissive geometry produces a black physical image; optional axes and wireframe are separate diagnostics.

Diffuse uses Lambert scattering, metal uses GGX with colored normal-incidence Fresnel reflectance, and glass supports smooth or GGX reflection/transmission with dielectric Fresnel and total internal reflection. Glass assumes closed, outward-wound meshes with air outside. Its baseColor is a surface transmission tint; volumetric absorption and nested media are not implemented. Emission is additive to any material model and defaults to one-sided; `twoSidedEmission` enables both sides.

Materials default to diffuse, baseColor `{0.8, 0.8, 0.8}`, roughness `0.5`, IOR `1.5`, and zero emission. BaseColor/roughness must be finite in [0, 1], IOR in [1, 10], and emission finite/nonnegative. Enabled Geometry requires Material. An empty mesh handle disables that instance; absent Transform supplies identity local pose while retaining ancestor transforms.

`assets.add<T>(resource)` stores an immutable CPU snapshot, `get(handle)` returns `const T&`, and `share(handle)` returns an owning `shared_ptr<const T>` for detached snapshots. Acquire on the owning thread; workers use retained immutable resources. Handles themselves remain weak, and empty/expired/foreign handles are rejected. Meshes have position/normal vertices and uint32 triangle indices; every three indices form one triangle. Empty and degenerate meshes have no hittable surface. Mesh line/topology APIs have been removed.

`Mesh::cube(size)`, `Mesh::sphere(radius, segments, rings)` and `Mesh::triangle(a, b, c)` remain triangle builders. Sphere defaults to radius 0.5, 32 segments, 16 rings and produces a closed mesh with 482 vertices and 960 triangles. Shared mesh instances have independent Transform and Material without copying vertex data. Each unique mesh receives a local BVH, with another BVH over world-space instances.

OBJ loading supports positive/negative indices, common face forms, triangles and convex polygons. Explicit normals are normalized; absent normals use area-weighted smoothing. UVs, materials and smoothing groups are not retained. Triangulate concave polygons before loading. Canonical paths share a cache entry; parse/file errors include path and line. The bundled bunny has 453 vertices and 948 triangles; see [asset terms](assets/README.md).

## Rendering configuration and extension

Renderer registration is explicit and must occur before Run. `Application::use<T>(args...)` owns one backend, replaces an existing backend and returns its reference. T derives from `Jade::Renderer::Backend`. Replacement and algorithm configuration during Run are rejected.

```cpp
Jade::Renderer::RayTracingConfig render;
render.resolutionScale = 1; // Actual framebuffer dimensions, not fullscreen mode.
render.maxSamples = 0;     // Unlimited; a positive value stops a static revision.
render.workerCount = 0;    // Auto: logical processors minus one, clamped to 1..256.
render.maxBounces = 12;
render.rrStartBounce = 4;
render.seed = 0;
render.exposureEV = 0;
app.use<Jade::Renderer::RayTracing>(render);
```

Resolution scale is within (0, 8], exposure within [-64, 64] EV, workerCount within [0, 256], maxBounces within [1, 1024], and roulette start positive. Fewer pixels improve CPU preview latency. Full framebuffer rendering is the default; no realtime full-resolution sample rate is promised.

The window thread captures immutable scene/camera state. A coordinator builds/reuses BVHs and schedules 16x16 tiles on persistent CPU workers. A sample round is committed only when complete. Static scenes accumulate; direct component edits reset accumulation for the new revision. During motion, the current round finishes and only the latest pending snapshot is rendered next. The previous complete image may remain visible while the next is computed; samples from different revisions are never blended. Scene/camera invalidation, resize and mode changes reject stale display results.

F enables unlit triangle edges and cancels ray computation; switching back begins fresh accumulation. F3 axes are composed after the image and do not affect Film or illumination. Projection O resets camera sampling. Minimized windows suspend work, and stopping Run joins active computation. `app.renderProgress()` returns revision, samplesPerPixel, render dimensions, sample time and inFlight; these are independent of presentation FPS.

Rendering can also run headlessly through public interfaces:

```cpp
auto snapshot = Jade::Renderer::SceneSnapshot::capture(*scene, app.assets());
auto prepared = Jade::Renderer::RenderScene::build(snapshot);
Jade::Renderer::Film film({320, 200});
Jade::Renderer::RayTracing renderer;
for (std::uint64_t sample = 0; sample < 64; ++sample) {
  renderer.render(prepared, {*snapshot.camera, film.size(), sample, 42, {}}, film);
}
auto image = film.resolve(); // Top-left origin, opaque sRGB RGBA8.
```

Backend computes one sample round into linear HDR Film, with no window access. RenderRequest supplies camera, size, sample index, seed and cancellation token. Custom Integrator implementations receive Ray, immutable RenderScene and Sampler; install one with `RayTracing::setIntegrator`. Integrator calls run concurrently. Sampler streams are independent of worker scheduling, so identical seed/settings/sample counts produce identical images across thread counts. RayQuery exposes closest-hit and occlusion interfaces. Scene contains no BVH or intersection method.

PathIntegrator samples both BSDFs and emissive triangles with MIS, traces secondary rays beyond camera clipping planes, and uses Russian roulette. Numerical ray offsets derive from hit-position error and floating-point rounding. `Film::resolve` applies exposure, Reinhard tone mapping and sRGB encoding once; OpenGL does not repeat color conversion. Textures, volumes, nested media, spectral transport, denoising and dedicated caustic techniques remain outside this version.

## Camera and transforms

Create a camera Entity explicitly with Transform and Camera, then call `scene->setCamera(camera)`. Camera defaults are perspective FOV 60 degrees, orthographic height 6, near 0.1 and far 100. Adding Camera never activates it automatically.

```cpp
auto camera = scene->camera();
auto& pose = camera.get<Jade::Transform>();
auto& lens = camera.get<Jade::Camera>();
pose.position = {4, 3, 8};
Jade::Motion::lookAt(pose, {0, 0, 0});
lens.projection = Jade::Projection::Orthographic;
lens.viewHeight = 6;
```

The right-handed world has Y up; an identity rotation faces local -Z with right +X and up +Y. Each local model applies scale, rotation, then translation. Camera position inherits the full parent matrix; orientation composes ancestor quaternions, ignoring ancestor scale. Its own scale is ignored. Moving position keeps orientation; `Motion::lookAt()` replaces rotation once and retains no target. Its target/up and movement use parent coordinates (world coordinates for roots). Quaternion angles use degrees through `Quaternion::angleAxis(axis, degrees)`. Motion yaw/pitch/roll compose about the current local axes, permit complete flips and normalize the result. Positive yaw turns right, pitch looks up and roll banks clockwise.

Perspective makes distant geometry smaller; orthographic keeps its size. FOV controls perspective vertical coverage, and viewHeight controls orthographic vertical coverage. Resize adjusts horizontal coverage from the actual framebuffer aspect ratio. Both lens values must stay valid, even when inactive. Positions/quaternions must be finite, rotations nonzero, scale positive, FOV strictly (0, 180), heights/clipping distances positive and far greater than near. Since components are plain data, consumers validate them when used. Invalid render data throws from Run.

`scene->setCamera()` rejects foreign/dead entities and missing components. If the selected camera is later destroyed or loses Transform/Camera, Run keeps processing input and clearing/presenting without world or axis drawing. Selecting a valid camera resumes rendering.

## Scene modules and controls

Use an engine module to install the camera controls shown in Demo:

```cpp
#include <Jade/Module/CameraControl.hpp>

scene->use<Jade::Module::CameraControl>();
```

CameraControl always targets `scene->camera()` at event time, so changing the selected camera needs no rebinding. It can be installed before creating a camera and skips missing components. Its scene-owned callbacks reset dragging on scene activation/deactivation, a changed camera and left-button release (including focus loss).

| Input | CameraControl action |
| --- | --- |
| WASD / arrows | Local forward/back/right/left, 3 parent units/second |
| Shift+W / Ctrl+W | Forward, 6 / 1 parent units/second |
| Space / Ctrl+Space | Parent Y up/down, 3 units/second |
| Q / E | Roll, -60 / +60 degrees/second |
| Left-button drag | Yaw/pitch, 0.1 degrees/pixel |
| Wheel | Zoom active lens, one Motion::zoom increment per offset |

These defaults are fixed in this version. Keyboard modifiers match exactly. Register custom scene events instead when different bindings are needed.

Any type providing `static void attach(Jade::Scene&)` can be a module; no instance is constructed:

```cpp
struct Spin {
  static void attach(Jade::Scene& scene)
  {
    scene.on<Jade::FrameUpdate>([&scene](const auto& frame) {
      for (auto entity : scene.view<Jade::Transform>()) {
        Jade::Motion::yaw(entity.get<Jade::Transform>(), 15 * frame.dt);
      }
    });
  }
};
scene->use<Spin>();
```

`use<T>()` installs once per type per Scene; repeated calls do nothing, including after reactivation. Recursive installation of the same type throws `std::logic_error`. If attach throws, new scene subscriptions are cancelled and new nested module installation records are removed, permitting retry; pre-existing subscriptions and modules remain. Other side effects, such as component changes or external registrations, are the module's responsibility. There is no module unload API; callbacks live with Scene.

`ApplicationConfig::debug` defaults to false in both Debug and Release builds. Setting it to true enables unmodified F (wireframe), F3 (axes) and O (current scene camera projection), once per press. Global and scene callbacks run first; Stop, app.stop() or a closed window suppresses the default. O skips an absent/incomplete camera and preserves pose, both zoom settings and clipping planes. Toggle diagnostics use the Application logger at Info. `config.axisGizmo` still controls initial visibility independently. `app.setWireframe()` / `wireframeEnabled()` and `app.setAxisGizmo()` / `axisGizmoEnabled()` work with either flag value. This flag does not change logging; terminal logging still defaults to Debug.

## Events and controls

For custom behavior, register scene events directly:

```cpp
auto camera = scene->camera();
scene->on<Jade::KeyHeld<"w">>([camera](const auto& event) {
  if (camera.has<Jade::Transform>()) {
    Jade::Motion::forward(camera.get<Jade::Transform>(), 3.0f * event.dt);
  }
});
scene->on<Jade::MouseScrolled>([camera](const auto& mouse) {
  if (camera.has<Jade::Camera>()) {
    Jade::Motion::zoom(camera.get<Jade::Camera>(), mouse.dy);
  }
});
```

Pressed/Released fire once per transition, including taps between frames. Held fires each submitted frame independently of OS repeat. FrameUpdate follows held keys/buttons, before rendering. First-frame dt is zero; later values are seconds between frame starts. Multiply movement speed by dt. Mouse pixel and wheel deltas already describe displacement; do not multiply by dt.

Keys are case-insensitive, including Space, arrows, letters, digits, F1-F12 and navigation/modifier keys. C-, S-, A-, M- denote Control, Shift, Alt, Super; matching is exact. Mouse buttons include Left, Right, Middle and Button4-Button8 and match regardless of keyboard modifiers. Invalid bindings fail compilation. Focus loss releases keys/buttons and resets cursor deltas. Callbacks run on the main thread in registration order. Returning EventFlow::Stop stops propagation; EventConnection::disconnect() cancels a subscription. `app.stop()` permits a later Run. See [Events](docs/Events.md) for details and dragging examples.

Motion translation/rotation takes Transform; zoom takes Camera. Movement defaults to 0.1 units per call. Forward/right follow local axes; up/down follow parent Y. Positive wheel offsets magnify the active lens parameter, keeping pose and clipping planes intact. O changes `lens.projection`, retaining both modes' zoom values.

The `Events/Application` and `Events/Scene:<id>` Loggers record subscription/cancellation and key/button edges at Debug.
Dispatch records include canonical bindings, the number of handlers actually called
and the consuming subscription ID (`consumed_by=0` means continued propagation).
Held, cursor, scroll and FrameUpdate details use Trace. Callback failures use Error
with the subscription and original exception message, then rethrow unchanged.
Explicit cancellation is logged when the next connection/dispatch prunes it.
Events follows the global logging threshold independently of `logPerformance`.

## Scene switching

```cpp
app.on<Jade::KeyPressed<"2">>([&] {
  app.setScene(otherScene);
  return Jade::EventFlow::Stop;
});
scene->on<Jade::SceneDeactivated>([&] { dragging = false; });
scene->on<Jade::SceneActivated>([&] { dragging = false; });
```

`app.on` handles global events first; the active scene's `on` handles them next, followed by unconsumed Escape and enabled F/F3/O defaults. Inactive scenes retain state and subscriptions but receive no input/update events. During Run, selection is queued until the next processing pass; the current pass uses one retained scene for input, update and rendering. The last request wins. Outside Run it commits immediately. Selecting the current scene emits no lifecycle events.

Deactivation runs before committing the pointer; activation runs after. Lifecycle Stop ends listener propagation but does not cancel the switch. Reentrant selections queue another switch. A deactivation exception retains the old scene; an activation exception retains the new scene. Both discard pending requests and propagate the original exception. Application destruction releases ownership without calling lifecycle callbacks. One scene can be active/reserved in only one Application at a time.

The first frame after switching has dt=0; elapsed and submitted frame indices continue through the Run. Physical held input persists, without synthetic Pressed/Released events. Reset transient state such as dragging in lifecycle handlers. See [Events](docs/Events.md) for lifetime and timing details.

## Public headers

Public interfaces are split by purpose, with one optional includes-only entry point, Jade.hpp:

| Folder/header | Interface |
| --- | --- |
| Core/ | Scene, SceneView, Entity, Transform |
| Math/ | Vector (Vec2/Vec3), Quaternion, Matrix4 |
| Graphics/ | Mesh, Vertex, Geometry, Material, Camera, Motion |
| Renderer/ | Backend, RayTracing, Integrator, BSDF, RayQuery, snapshots, Sampler, Film and Image |
| Assets/ | Assets, AssetHandle and MeshHandle |
| Input/, Events/ | Key/button bindings and event contracts |
| Application.hpp, Color.hpp, Log.hpp, WindowConfig.hpp | Application loop and value/configuration types |

There are no folder aggregation headers. Source `.cpp` files include their matching private `.hpp` first and are explicitly listed in CMake. Registry storage, RenderSession, OpenGL display, OBJ parsing, input state, event dispatch, matrix construction and clock are internal. Consumers receive only `Jade/include`.

## Frame scheduling and logs

`config.frameLimits` defaults to 24 frames per second and must be positive. The loop uses monotonic absolute deadlines and waits for events between frames; slow frames skip missed slots. VSync defaults to false and may further constrain the achieved rate. `app.setWireframe(bool)` selects the unlit debug mode and bypasses ray computation; unit world axes draw over the presented image without changing physical samples. Demo starts with axes disabled.

```text
15:27:07.591522  INFO [Application] - Ready: window=960x640 target_fps=24 vsync=off
15:27:07.592123 DEBUG [Assets] - Loaded mesh: path=assets/bunny.obj vertices=453 triangles=948 duration=1.20ms
15:27:07.593456  INFO [Application] - Run started
15:27:08.635123 DEBUG [Performance] - fps=24.0 interval_avg=41.67ms interval_max=44.31ms work_avg=2.10ms work_max=5.30ms
```

Application records readiness, Run start/stop and exception aborts; Assets records
successful file loads at Debug and failures at Error while preserving exceptions.
Cache hits do not repeat successful load messages. Axis Gizmo, camera projection
and Demo wireframe changes are event logs. Each Run resets timing and retains the selected Scene,
Assets and subscriptions. `RunOptions::frameLimit` limits total submitted frames
independently of FPS. Completed sample progress is available through `renderProgress()` and periodic Debug logs in the RayTracing category.

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

Tests exercise public Scene, Assets and component APIs; path-tracing tests run headlessly; presentation and debug tests additionally use internal Window/OpenGL display and GLFW pixel readback. Window/presentation checks require an OpenGL 3.3-capable runtime; computation needs no GPU. They do not require a particular Visual Studio installation path, Mesa installation, Windows message-injection script or preinstalled GoogleTest package. A graphics driver can provide OpenGL; Windows machines without one can optionally use the local Mesa DLLs described in [runtime/README.md](runtime/README.md).

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
Jade/include/Jade/   # Core/, Math/, Graphics/, Renderer/, Assets/, Input/, Events/
Jade/src/Core/       # Application, Entity, Scene, Log
Jade/src/Math/       # Matrix4, Quaternion
Jade/src/Graphics/   # Mesh, Motion, builtins/Cube and Sphere
Jade/src/Assets/     # Shared resource storage
Jade/src/Renderer/   # CPU BVH, BSDF, path integration, sampling and Film
Jade/src/internal/   # Registry, RenderSession, OpenGL display, platform and input
Demo/               # Two scenes, cube/bunny and animated hierarchy
assets/             # Bunny OBJ and attribution
tests/              # ECS, assets, math, input, scheduling and pixel checks
```

GoogleTest exercises identity generations, ownership, hierarchy, scene switching, component/query mutation, shared immutable resources, OBJ loading, quaternion controls, projections, resize, ray intersections, BSDFs, emissive lighting, deterministic CPU sampling, asynchronous revisions, wireframe, axes and events; OpenGL readback validates presentation/debug output. Tests build independently of Demo. See [Architecture](docs/Architecture.md) for boundaries and [Validation](docs/Validation.md) for recorded checks.
