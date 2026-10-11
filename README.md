# Jade

Jade is a C++20 static engine library validated on Windows x64 / MSVC. Application owns the window/input loop; Runtime coordinates Scene, Assets, Renderer and Physics through a shared categorized Job System. Scene owns component data and signature-indexed entity groups. Physics supports Box/Sphere rigid bodies, gravity, angular motion and persistent multi-point contact solving without a window or GPU. Renderer provides CPU progressive path tracing and unlit wireframe; OpenGL 3.3 presents completed images and debug primitives. Include individual public headers or `<Jade/Jade.hpp>` and link `Jade::Jade`.

## Render pipelines

Untitled starts in Raster. I cycles Raster ? Hybrid ? RayTracing ? None; F1 wireframe
and F3/F4 debug drawing remain independent. Hybrid uses raster geometry with real BVH
shadow rays; RayTracing retains the existing CPU/GPU path tracer. F5 toggles an independent normal debug view, including when I selects None, without changing the selected pipeline. EngineConfig.renderer.pipeline selects startup;
the engine's default remains RayTracing for existing consumers. The old ray enable API
is a compatibility adapter: true selects RayTracing and false selects None.

F2 enables the soul observation camera when debug shortcuts are enabled. The world keeps
updating, while WASD, Space/Shift, right-button look, middle-button pan and wheel zoom control an independent observer.
Sparse diagnostic paths come from the game camera. See [Untitled](Demo/Untitled/README.md)
for controls, pipeline options and rendering limits.

## Build

Install the Visual Studio or Build Tools **Desktop development with C++** workload, including MSVC x64, the Windows SDK, CMake and Ninja. The first configuration downloads pinned GLFW and GoogleTest dependencies; subsequent configurations reuse each build directory's downloads. The OpenGL loader is included as fixed C source, and shader embedding uses CMake directly. No Python installation or packages are required.

Open the repository folder in Visual Studio to discover the Windows Debug / Release presets and select `JadeDemo.exe` as the startup target. In VS Code, run `code .` from the repository root and install the recommended CMake Tools and C/C++ extensions. Select the `Windows x64 Debug` configure preset when prompted. The checked-in `.vscode` configuration provides IntelliSense through CMake Tools, Ctrl+Shift+B to configure/build JadeDemo, and F5 to build/debug the selected JadeDemo, Untitled or JadeSmokeTests launch target. JadeDemo starts in scene 1; each launch uses its executable directory as the working directory. For command-line builds, use an **x64 Native Tools Command Prompt** or an x64 **Developer PowerShell**:

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

`Demo/Untitled` is the playable HD-2D game prototype. Build/run `Untitled.exe` for
camera-relative WASD movement, Q/E eight-direction camera rotation, Idle/Walk
sprites, collision and path-traced shadows. It caps presentation at 24 FPS, defaults
to GPU PathTrace at 480×320 inside a 960×640 window, and enables engine debug shortcuts.
All thirteen Hormelz character animation packs are bundled inside its own Assets folder.
See [Untitled](Demo/Untitled/README.md) for controls, performance options and asset details.
The original `JadeDemo` showcase has moved to `Demo/Showcase`, preserving its scenes and CLI.

```powershell
& ./build/windows-debug/bin/JadeDemo.exe

# Build the engine and tests without Demo.
cmake --preset windows-debug -B build/no-demo-debug -DJADE_BUILD_DEMO=OFF -DBUILD_TESTING=ON
cmake --build build/no-demo-debug --parallel
& ./build/no-demo-debug/bin/JadeSmokeTests.exe
```

`JADE_BUILD_DEMO` and `BUILD_TESTING` independently control Demo and tests; disable both to build only the engine and its dependencies.

## Scenes and entities

Demo starts with image rendering off and Wireframe on in Scene 3: a 5/3/1 pyramid and a falling ball above a static floor. G switches Earth/Moon gravity and K gives the top cube an off-center impulse. Scene 1 is the bunny/metal/glass showroom. Scene 2 is a mutually attracting three-body orbit, driven by mass, initial velocity and VelocityVerlet; it has no parenting or scripted rotation. Its scaled masses are 100/10/0.1, distances 8/0.9, G=1 and softening=0.001. Scene 4 preserves the destruction wall and heavy glass projectile; K fires it from the current camera. Select scenes with 1/2/3/4/5, pause physics with P and rebuild the active physics scene with R. Scene 3 reset retains the selected gravity. Scenes retain state while inactive. Launch with `JadeDemo --scene 1|2|3|4|5`; `--frames N` can be combined with it. F1 toggles wireframe and I cycles Raster/Hybrid/RayTracing/None.

Scene 5 is a stress test: ten parallel walls, each made of 100 x 100 dynamic unit cubes (100,000 cubes total), spaced 12 units apart and sharing one mesh. A radius-three, mass-3500 glass ball automatically launches toward them at 60 units/second; K fires it again from the current camera. The scene is created on first selection. Its camera frames all ten walls, has a 1000-unit far plane and moves at 30 units/second (5 with Ctrl). R rebuilds the walls and restarts the launch. This workload can run below realtime; no frame-rate target is guaranteed.

F3 toggles axes and cyan AABBs together. F5 toggles the independent normal debug view, including with the scene pipeline set to None. F4 toggles orange Collider outlines, red contact markers, green normal arrows, blue field-force arrows, yellow total-force arrows and Physics/Debug logging. Blue/yellow arrows show direction using lengths 0.8/0.6, so coincident directions remain visible; near-zero forces are hidden. A supported resting body retains downward field force while total force is approximately zero. Scene selection preserves render/debug switches. Demo RayTracing uses resolutionScale 1.0; presentation targets 24 FPS. WASD/arrows move horizontally, Space ascends, Shift descends, Ctrl slows movement, right dragging turns, the wheel zooms and middle dragging pans either projection. Q/E are unbound. O switches projection and Esc exits. Demo uses Archetype descriptors and shared meshes.

```cpp
#include <Jade/Jade.hpp>

int main()
{
  Jade::EngineConfig config;
  config.debug.shortcuts = true; // Enable Application F2/F3/F4/F5/O shortcuts; F1/I always work.
  Jade::Application app(config);
  auto scene = std::make_shared<Jade::Scene>();

  auto camera = scene->create();
  auto& pose = camera.add<Jade::Transform>();
  pose.position = {4, 3, 8};
  Jade::Motion::lookAt(pose, {0, 0, 0});
  camera.add<Jade::Camera>();
  scene->setCamera(camera);
  scene->use<Jade::Module::FPSCameraControl>();

  const auto mesh = app.runtime().assets().add(Jade::MeshData::cube(2.0f));
  auto cube = scene->create();
  cube.add<Jade::Transform>();
  cube.add<Jade::Geometry>(mesh);
  cube.add<Jade::Material>(Jade::Materials::Gold());

  auto lamp = scene->create();
  auto& lampPose = lamp.add<Jade::Transform>();
  lampPose.position = {0, 4, 0};
  lampPose.scale = {1.5f, 0.05f, 1.5f};
  lamp.add<Jade::Geometry>(mesh);
  lamp.add<Jade::Material>(Jade::Materials::Emissive({14, 12, 10}));

  app.runtime().setScene(scene);
  app.Run();
}
```

Runtime starts without a scene; a new Scene is empty, requires no window and creates no implicit camera. Assets remain shared at Runtime scope. `app.runtime().scene()` returns the active `shared_ptr<Scene>`; `app.runtime().setScene(nullptr)` unloads it and leaves the application clearing/presenting and dispatching global events.

Components are ordinary C++ types; no inheritance is required:

```cpp
struct Velocity { Jade::Vec3 value; };

auto entity = scene->create();
entity.add<Jade::Transform>();
entity.add<Velocity>(Jade::Vec3{1, 0, 0});
for (auto entity : scene->query<Jade::Transform, Velocity>()) {
  auto& pose = entity.get<Jade::Transform>();
  pose.position = pose.position + entity.get<Velocity>().value * 0.01f;
}
```

`add<T>(args...)` constructs a component and returns its reference. `get<T>()`, `has<T>()` and `remove<T>()` access state; duplicate add throws `std::logic_error`, missing get throws `std::out_of_range`, and removing an absent component is a no-op. `scene->destroy(entity)` destroys the entity and its descendants. All subtree handles become invalid before component destructors run; components are released children before parents. Generation IDs prevent old handles from accessing reused slots. Handles and views do not extend Scene lifetime. Component references survive unrelated creation, but removal and owner destruction invalidate them. A const Entity handle still accesses mutable components.

`scene->query<T...>()` snapshots matching identities. New entities or new matches wait until the next query; destroyed entities and removed matches are skipped when advancing the iterator. Keep a named view alive while using its iterators. Removing/replacing a component invalidates its references immediately. Scene mutation and rendering use the main thread.

## Runtime, configuration and Archetype

```cpp
Jade::EngineConfig config;
config.physics.fixedStep = 1.0 / 120.0;
config.renderer.rayTracing.enabled = false;
config.renderer.wireframe = true;
config.debug.shortcuts = true;
Jade::Application app(config);
auto& runtime = app.runtime();
// runtime.assets(), runtime.scene(), runtime.renderer(), runtime.physics()
auto scene = std::make_shared<Jade::Scene>();
scene->physics().fields = {
  std::make_shared<const Jade::Physics::UniformGravity>(Jade::Vec3{0, -9.81f, 0})};
runtime.setScene(scene);
```

Renderer has independent `setRayTracingEnabled(bool)` / `rayTracingEnabled()` and `setWireframe(bool)` / `wireframeEnabled()` controls. F1 toggles Mesh wireframe; I toggles image rendering, regardless of debug-shortcut settings. Sampling configuration and progress are available through `runtime.renderer().rayTracingSettings()` / `setRayTracingSettings()` and `runtime.renderer().progress()`. Changing only Wireframe preserves path-tracing samples. Disabling image rendering stops/joins ray work and clears its image; re-enabling starts fresh samples. RayTracingSettings.enabled defaults to true, wireframe to false; Demo starts with ray tracing disabled and wireframe enabled.

| rayTracing.enabled | wireframe | Scene presentation |
| --- | --- | --- |
| true | false | RayTracing image |
| true | true | RayTracing image with mesh edges |
| false | true | Mesh edges |
| false | false | Cleared background |

Axes and physics debug drawing remain independently available in all four states. Physics is independently usable as `Jade::Physics::System` with `step(scene, dt)`. Runtime accumulates frame time into fixed steps after FrameUpdate and before rendering. At most eight substeps run per frame; excess whole steps are discarded and logged. Scene selection, Run boundaries and `runtime.setPaused(...)` reset accumulated time and contact caches. Inactive scenes do not simulate.

Each Scene owns a `Physics::Environment` through `scene.physics()`. Its immutable `ForceField` objects sample world-space center forces from a read-only body snapshot. `GravityField::acceleration` returns acceleration; its final `sample` multiplies by target mass. Thus identical gravity gives different forces but the same acceleration to different masses. New scenes default to Earth `UniformGravity`; replace it with `{0,-1.62f,0}` for Moon gravity or clear `fields` for no field force. `Physics::Config` contains clock and solver settings; the old `gravity` member is removed.

`PointGravity(center, mass, G, softening)` supplies a fixed source; `CompositeGravity` sums child accelerations. `BodyGravity(sources, G, softening)` opts selected root dynamic entities into source generation, reading source mass and position from each stage snapshot and excluding self-gravity. Destroyed sources expire without attaching to recycled identities; live foreign or malformed sources throw. Point sources use `G*M*r/(r²+softening²)^(3/2)` with positive softening (default 0.001). Fields must be immutable and safe for concurrent sampling; `validate(scene)` runs on the owning thread before sampling.

`Environment::integration` defaults to `SemiImplicitEuler`. `VelocityVerlet` takes a half velocity kick, moves all bodies, resamples a consistent new-position snapshot, then completes the kick before contact solving. Dynamics, collision and contact solving have separate internal implementations; the existing Job System still schedules integration, bounds, broad/narrow phase and islands. Any invalid input, field sample or output rejects the step before Scene components, statistics or caches are committed.

`physics.forceSnapshot(scene)` returns per-body average field force, contact force and total force over the last successful step. Contact force is the net applied normal/friction impulse divided by dt, including warm starting once; positional corrections and separately applied instantaneous impulses are excluded. A stale snapshot supplies fresh field force with `solved=false`; total force remains unavailable until the next successful step.

```cpp
using Object = Jade::Archetype<Jade::Geometry, Jade::Transform, Jade::Material>;
using Physical = Jade::Archetype<Jade::Transform, Jade::Collider, Jade::RigidBody>;
struct Ball {
  using Components = Jade::Archetype<Object, Physical>;
};
Jade::Entity ball = scene->create<Ball>();
// Geometry.mesh and Collider are initially empty; bind them when ready.
ball.get<Jade::Geometry>().mesh = sphereMesh;
ball.get<Jade::Collider>() = Jade::Collider{Jade::Sphere{0.5f}};
ball.get<Jade::Transform>().position = {0, 2, 0};
ball.get<Jade::RigidBody>().mass = 2;
ball.get<Jade::RigidBody>().velocity = {1, 0, 0};
for (auto entity : scene->query<Physical>()) { /* includes manually composed entities */ }
ball.get<Jade::Collider>() = {}; // Suspend physics without removing components.
```

Archetype is a type-only component description. Use an alias such as `using Ball = Jade::Archetype<Object, Physical>` or a named descriptor with `using Components = ...`; both use the same creation and query mechanism. Composition recursively flattens descriptors and deduplicates components in first-appearance order. `scene.create<T>()` returns Entity and default-constructs exactly the declared components, without instantiating the descriptor or running its constructor. Transform must be declared explicitly. Every declared component must be default-constructible; a throwing component constructor rolls back the new entity. Configure values through `get<T>()` after creation; non-default-constructible components can still be added manually with `add<T>(args...)`.

An empty `Geometry.mesh` is skipped by rendering, and `Collider{}` is skipped by collision queries, simulation, debug drawing and physics logs. Material retains its valid default parameters. Component queries match presence, including components with empty resources; binding or clearing a resource does not change the signature. Dynamic add/remove remains supported. `scene.create<Jade::Archetype<>>()` creates an empty entity, and `scene.query<Jade::Archetype<>>()` matches all live entities. Queries cache matching component signatures and enumerate those groups instead of scanning all entity slots. Components remain in stable type pools, not packed movable component chunks. The previous Entity-derived Archetype handles and `create<T>(args...)` constructor API are removed; configure the returned Entity instead.

RigidBody uses world-space velocity and angularVelocity (radians/second), mass in kg, friction 0.6 and restitution 0 by default. An empty Collider suspends participation, preserving pose and velocities. `collider.empty()` reports this state; assigning a Box/Sphere Collider activates it. A nonempty Collider without RigidBody, or with mass 0, is static. Participating positive-mass bodies require a root Transform and Collider; parented dynamic bodies are rejected. Inertia is derived from the scaled Box/Sphere. `runtime.physics().applyImpulse(scene, entity, impulse, worldPoint)` changes both linear and angular velocity for off-center impulses; an empty Collider throws invalid_argument without changing velocities.

The solver uses semi-implicit integration, up to four Box face contacts, speculative contact retention (5 mm), accumulated normal/tangent impulses, warm starting and Coulomb friction. Defaults are 20 velocity iterations, four separate position-correction iterations and 2 mm penetration slop. Restitution applies only above a 1 m/s closing speed. Static/static pairs remain available to geometric queries/debug drawing but do not enter the dynamics solver. No sleeping, CCD, joints, kinematic bodies or dedicated physics thread are introduced. Runtime Physics uses the shared Job System; independently constructed Physics::System remains serial unless a JobSystem is injected.

```cpp
auto root = scene->create(); // An identity group needs no Transform.
auto child = scene->create();
child.add<Jade::Transform>().position = {2, 0, 0};
scene->setParent(child, root);
auto matrix = scene->worldMatrix(child); // Jade::Matrix4, column-major.
scene->setParent(child);                 // Detach, retaining local TRS.
```

Hierarchy belongs to Scene; Transform contains local position, rotation and scale. `parent(entity)` returns an empty Entity for a root; `children(entity)` returns an attachment-order snapshot. Self-parenting, cycles and foreign/dead entities are rejected before changing links. Removing Transform preserves links and makes that entity an identity group. Reparenting retains local TRS, so world pose can change. Full parent times local matrices preserve shear from nonuniform scale and rotation.

The World type/header, `Application::world()/camera()/setCamera()` and callback-based `each()` are removed. Use Scene directly; there are no compatibility aliases.

## Meshes, instances and materials

Every physical object is an instance of an immutable compiled triangle Mesh. Geometry references an immutable mesh resource; Material describes scattering and emission independently:

```cpp
const auto bunnyMesh = app.runtime().assets().load<Jade::Mesh>("assets/bunny.obj");
auto bunny = scene->create();
bunny.add<Jade::Transform>().scale = {16, 16, 16};
bunny.add<Jade::Geometry>(bunnyMesh);
bunny.add<Jade::Material>(Jade::Materials::Gold());

auto metal = Jade::Materials::Gold();
auto glass = Jade::Materials::Glass();
auto lamp = Jade::Materials::Emissive({14, 12, 10});

// Presets return independent values and can be adjusted per entity.
auto copper = Jade::Materials::Copper();
copper.roughness = 0.35f;
```

All illumination comes from emission on actual geometry. There are no point/directional light entities, ambient term or background illumination. Escaped rays return black. A scene without emissive geometry produces a black physical image; optional axes and wireframe are separate diagnostics.

Diffuse uses Lambert scattering, metal uses GGX with colored normal-incidence Fresnel reflectance, and glass supports smooth or GGX reflection/transmission with dielectric Fresnel and total internal reflection. Glass assumes closed, outward-wound meshes with air outside. Its baseColor is a surface transmission tint; volumetric absorption and nested media are not implemented. Emission is additive to any material model and defaults to one-sided; `twoSidedEmission` enables both sides.

Available presets are `Materials::Diffuse(color)`, `Metal(reflectance, roughness)`, `Gold(roughness)`, `Silver(roughness)`, `Copper(roughness)`, `Glass(roughness, ior, tint)` and `Emissive(radiance, twoSided)`. Named metal defaults use roughness 0.18 and artist-facing linear RGB reflectance approximations; glass defaults to smooth white transmission with IOR 1.5. Emissive defaults to one-sided `{10,10,10}` emission and zero diffuse reflectance. All presets return plain Material values and use the same validation-at-use contract as manually authored materials.

Materials default to diffuse, baseColor `{0.8, 0.8, 0.8}`, roughness `0.5`, IOR `1.5`, and zero emission. BaseColor/roughness must be finite in [0, 1], IOR in [1, 10], and emission finite/nonnegative. Enabled Geometry requires Material. An empty mesh handle disables that instance; absent Transform supplies identity local pose while retaining ancestor transforms.

Include `<Jade/Materials/Material.hpp>` for the physical description and `<Jade/Materials/Presets.hpp>` for preset factories. Material stores values only. RayTracing builds a CPU BSDF from those values. The public material owns no shader, program or pipeline identifier.

Only four GLSL files remain: Display.vert and ImageDisplay.frag present completed sRGB images; Lines.vert and Lines.frag draw wireframe and axes. They belong to the internal presentation/debug layers and use PascalCase filenames. Embedded C++ names include the stage, such as DisplayVertex and LinesFragment.

`assets.add(MeshData)` validates and compiles geometry plus its SAH BLAS before publishing a MeshHandle. `Mesh::compile(MeshData)` performs the same compilation without Assets; `assets.add<Mesh>(compiled)` registers an already compiled resource. Copies of Mesh share their immutable data and BLAS. Other `assets.add<T>(resource)` calls store immutable CPU snapshots; `get(handle)` returns `const T&`, and `share(handle)` returns an owning `shared_ptr<const T>` for detached snapshots. Acquire on the owning thread; workers use retained immutable resources. Handles themselves remain weak, and empty/expired/foreign handles are rejected. `MeshData` has editable position/normal vertices and uint32 triangle indices; `Mesh::data() const` exposes them read-only after compilation; every three indices form one triangle. Empty and degenerate meshes have no hittable surface. Mesh line/topology APIs have been removed.

`MeshData::cube(size)`, `MeshData::sphere(radius, segments, rings)` and `MeshData::triangle(a, b, c)` remain triangle builders. Sphere defaults to radius 0.5, 32 segments, 16 rings and produces a closed mesh with 482 vertices and 960 triangles. Shared mesh instances have independent Transform and Material without copying vertex data. Each compiled mesh owns its local BLAS. Scene compilation creates a separate TLAS over world-space instances; both use 16-bin SAH across all three axes.

OBJ loading parses MeshData, validates it and builds the Mesh BLAS before publishing the asset and canonical-path cache entry. Loading failures publish neither. OBJ parsing supports positive/negative indices, common face forms, triangles and convex polygons. Explicit normals are normalized; absent normals use area-weighted smoothing. UVs, materials and smoothing groups are not retained. Triangulate concave polygons before loading. Canonical paths share a cache entry; parse/file errors include path and line. The bundled bunny has 453 vertices and 948 triangles; see [asset terms](assets/README.md).

## Jobs and physics scheduling

Runtime owns a single `JobSystem`, shared by Physics, scene compilation and RayTracing. Set `config.jobs.workerCount` before constructing Application; zero automatically selects logical processors minus one, clamped to 1..256. `runtime.jobs()` exposes the same scheduler. Physics remains synchronous: `step(scene, dt)` returns only after private worker buffers have been committed on the owning thread.

```cpp
Jade::JobSystem jobs({3}); // Headless, three workers.
Jade::Physics::System physics(jobs);
Jade::Renderer::RayTracing rays(jobs);
std::vector<float> values(10000);
auto work = jobs.parallelFor(values.size(), [&](std::size_t i) {
  values[i] = static_cast<float>(i);
}, {Jade::JobCategory::Gameplay});
jobs.wait(work);

Jade::TaskGraph graph;
auto load = graph.add([] { /* Produce private CPU data. */ }, {Jade::JobCategory::Asset});
auto prepare = graph.add([] { /* Consume that CPU data. */ }, {Jade::JobCategory::Rendering});
graph.depends(prepare, load);
jobs.wait(jobs.submit(graph));
```

Categories identify Physics, Gameplay, Rendering, RayTracing, Asset and General work. Their default priorities are Critical, High, Normal and Background respectively; the last three categories share Background. `JobOptions::priority` overrides the default. `config.jobs.priorityWeights` defaults to `{8, 4, 2, 1}`: worker dispatch uses weighted rotation, with category rotation within a priority. Empty queues are skipped. Weights count dispatched work items, not CPU time, and executing callbacks are never preempted. Higher-priority graph consumers raise their prerequisites' effective priority.

Workers execute local deque tasks and steal from other workers; external submissions enter category queues. Parallel ranges keep bounded fan-out even with `grainSize=1`. Graph parallel nodes can obtain their count from a callback after prerequisites complete. Each graph submission owns an execution snapshot and can be waited independently. Worker waits cooperate with the scheduler; external `wait(handle, WaitMode::Help)` assists only that graph and descendant submissions. External assistance does not consume worker fairness budgets. Exceptions skip unstarted work in the failed group, drain running callbacks and rethrow from wait. Shutdown rejects external submissions, drains accepted work and internal descendants, and joins workers. Callbacks must terminate; a job cannot wait for itself/ancestors or shut down its own scheduler.

Physics uses Integration ? Bounds ? Sweep and Prune ? Narrow Phase ? Islands ? Solve ? Export dependencies. Candidate and contact ordering stays stable. Separate islands solve concurrently, preserving sequential iterations inside an island; shared static bodies are read-only. Contact anchors, warm starting, paused inspection and entity generations retain their existing contracts. Sparse broad phase avoids the full pair scan; dense overlap can still produce quadratic candidates. `statistics().candidatePairs` and `statistics().islands` expose that work.

Scene, Entity, component storage, Assets publication, input and OpenGL remain owning-thread APIs. Categories provide scheduling, not memory exclusion. Jobs must use immutable inputs or disjoint outputs and express data dependencies explicitly. Captures must outlive all handles using them. Injected systems require their JobSystem to outlive them; separate standalone RayTracing instances create their own generic Job Systems unless an external scheduler is supplied. RayTracing no longer has a `workerCount` configuration field. Asset categorization does not add an asynchronous asset-loading API.

## Rendering configuration and extension

Runtime's Renderer::System exposes one `RayTracingSettings` configuration for ray algorithms and output. Set it in EngineConfig before construction, or use `rayTracingSettings()` / `setRayTracingSettings()` on the window thread, including FrameUpdate. `rayTracing()` remains the CPU Integrator extension point outside Run.

```cpp
Jade::EngineConfig config;
config.renderer.rayTracing.device = Jade::Renderer::RayDevice::CPU;
config.renderer.rayTracing.mode = Jade::Renderer::RayMode::PathTrace;
config.renderer.rayTracing.enabled = true;
config.renderer.rayTracing.output = Jade::Renderer::RayOutput::Color;
config.renderer.rayTracing.jitter = true;
config.renderer.rayTracing.resolutionScale = 1; // Actual framebuffer dimensions.
config.renderer.rayTracing.maxSamples = 0;     // Unlimited; positive stops a static revision.
config.jobs.workerCount = 0;    // Auto: logical processors minus one, clamped to 1..256.
config.renderer.rayTracing.maxBounces = 12;
config.renderer.rayTracing.rrStartBounce = 4;
config.renderer.rayTracing.seed = 0;
config.renderer.rayTracing.exposureEV = 0;
Jade::Application app(config);
```

`RayDevice::CPU` / `GPU` selects execution; `RayMode::PathTrace` / `RayCast` selects transport or primary-ray visibility on either backend. `RayOutput::Color` evaluates the selected algorithm, and `RayOutput::Normal` displays primary-hit world normals. Surface BSDFs and PBR properties belong to Material. maxBounces and rrStartBounce control PathTrace on both CPU and GPU.

```cpp
auto& renderer = app.runtime().renderer();
auto settings = renderer.rayTracingSettings();
settings.exposureEV = 1;
settings.maxSamples = 128;
renderer.setRayTracingSettings(settings);
```

Exposure changes resolve the existing Film without retracing. Increasing maxSamples resumes paused accumulation or raises the per-frame sample cap; lowering it preserves completed samples and lets any current round finish. Changing device, mode, output, jitter, resolutionScale, seed or active path-depth settings cancels/joins work and starts a fresh Film on the next scene submission. Invalid settings are rejected before stopping the current renderer. Changing CPU path-depth settings installs the built-in PathIntegrator; other changes retain a custom CPU Integrator. Custom CPU Integrators are not translated into GPU shader code. Normal output bypasses the transport Integrator and uses direct `[0, 1]` RGB data encoding, without exposure, tone mapping or sRGB conversion.

Unmodified KeyPressed I toggles image rendering and F1 toggles Mesh wireframe after application and scene handlers. Both work with debug shortcuts disabled and respect EventFlow::Stop, modifiers and repeats. Wireframe overlays the current image without restarting samples. Disabling image rendering cancels/joins computation; re-enabling starts a fresh Film. Runtime owns the shared worker pool; rendering algorithms and immutable mesh acceleration survive render switches.

Resolution scale is within (0, 8], exposure within [-64, 64] EV, workerCount within [0, 256], maxBounces within [1, 1024], and roulette start positive. Fewer pixels improve CPU preview latency. Full framebuffer rendering is the default; no realtime full-resolution sample rate is promised.

Unmodified P toggles pause in every scene, independently of debug shortcuts, and respects event consumption and repeats. The window thread captures immutable scene/camera state. A coordinator submits scene compilation as Rendering work. The CPU backend schedules 16x16 tiles as RayTracing background work on the shared persistent workers; the GPU backend dispatches compute and publishes shared HDR textures. Compilation consumes already compiled meshes and reuses/refits/rebuilds the instance TLAS. Camera/material changes reuse the TLAS. Transform-only changes refit a detached copy; SAH cost exceeding 1.5 times the last complete build triggers a rebuild. Changes to instance layout or mesh references rebuild directly. A sample round is committed only when complete. While running, every application frame gets independent samples, even if the scene is unchanged. The renderer completes minSamplesPerFrame full-image rounds (default 4, capped by a nonzero maxSamples) before applying the frame-rate deadline. It adds further complete samples only while the frameLimits deadline and maxSamples cap allow it, estimating the next round from the previous duration. Demo defaults to 8 rounds; applications configure minSamplesPerFrame and maxSamples through RayTracingSettings. An over-budget round finishes in full and lowers actual FPS; frame boundaries never cancel half an image. Each running frame has a distinct deterministic random stream. P pauses simulation and switches rendering to progressive accumulation of the unchanged snapshot; resuming starts fresh frame samples. Paused camera/component edits reset accumulation. Samples from different frames or revisions are never blended. Scene/camera invalidation, resize and image-render switches reject stale display results.

Wireframe draws unlit triangle edges, independently of image rendering. F3 axes are composed afterward and do not affect Film or illumination. Projection O resets camera sampling. Minimized windows suspend work, and stopping Run joins active computation. app.runtime().renderer().progress() reports RayTracing, RayTracing + Wireframe, Wireframe or Disabled, with optional sample count, sample time and inFlight state. Samples are available whenever image rendering is enabled. These values are independent of presentation FPS.

Rendering can also run headlessly through public interfaces:

```cpp
auto snapshot = Jade::Renderer::SceneSnapshot::capture(*scene, app.runtime().assets());
auto prepared = Jade::Renderer::RayScene::build(snapshot);
Jade::Renderer::Film film({320, 200});
Jade::Renderer::RayTracing renderer;
for (std::uint64_t sample = 0; sample < 64; ++sample) {
  renderer.render(prepared, {*snapshot.camera, film.size(), sample, 42, {}}, film);
}
auto image = film.resolve(0, renderer.imageEncoding()); // Top-left origin, opaque RGBA8.
```

ProgressiveBackend defines a headless sample-round contract over RayScene, RenderRequest and linear HDR Film. Runtime's Renderer::System selects its CPU RayTracing or GPU ComputeRayTracer by RayDevice. CPU algorithm extension is through Integrator. Custom Integrator implementations receive Ray, immutable RayScene and Sampler; install one with app.runtime().renderer().rayTracing().setIntegrator or on a headless RayTracing instance. Integrator calls run concurrently. Sampler streams are independent of worker scheduling, so identical seed/settings/sample counts produce identical images across thread counts. RayQuery exposes closest-hit and occlusion interfaces. Scene contains no BVH or intersection method.

PathIntegrator samples both BSDFs and emissive triangles with MIS, traces secondary rays beyond camera clipping planes, and uses Russian roulette. Numerical ray offsets derive from hit-position error and floating-point rounding. `Film::resolve` applies exposure, Reinhard tone mapping and sRGB encoding for CPU/export images; runtime GPU presentation applies the same mapping directly to its HDR texture. Conversion occurs once on either route, and Normal output bypasses it. Textures, volumes, nested media, spectral transport, denoising and dedicated caustic techniques remain outside this version.

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

## GPU path tracing (OpenGL compute)

Select execution directly in Demo:

```text
JadeDemo --gpu --scene 1
JadeDemo --cpu --scene 1
JadeDemo --gpu --scene 3 --frames 8
```

`--cpu` and `--gpu` are mutually exclusive, select execution and set `rayTracing.enabled = true`. F5 toggles independent normal debug when debug shortcuts are enabled; it also draws with I set to None and preserves all ray tracing settings. Sampling and bounce controls belong to `RayTracingSettings`; Demo no longer exposes `--output`, `--bounces`, `--rr-start`, `--samples` or `--frame-samples`. `--frames N` limits application frames, `--scene 1|2|3|4` selects the initial scene and `--help` lists options. The old `--renderer` option is removed. Without an execution flag Demo retains its CPU-selected wireframe startup; I cycles Raster/Hybrid/RayTracing/None; F1 independently toggles wireframe.

```cpp
Jade::EngineConfig config;
config.renderer.rayTracing.device = Jade::Renderer::RayDevice::GPU;
config.renderer.rayTracing.mode = Jade::Renderer::RayMode::PathTrace;
config.renderer.rayTracing.maxBounces = 6;
config.renderer.rayTracing.rrStartBounce = 3;
config.renderer.rayTracing.resolutionScale = 0.5f;
Jade::Application app(config);
```

Demo ceiling lamps in the showroom, stack and destruction scenes emit only from their downward area panels. Sampling all six emitting box faces can otherwise return zero direct light for a surface below the lamp. This produces written black pixels at low sample counts. The higher frame minimum reduces remaining stochastic noise; it is not a denoiser.

ComputeRayTracer evaluates Lambertian diffuse, GGX metal, smooth/rough dielectric reflection and transmission, Fresnel/TIR, emissive triangles, shadow visibility, next-event estimation with power-heuristic MIS, and Russian roulette. These follow the existing CPU BSDF/PathIntegrator and the [PBRT path-integrator approach](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer). Camera clipping applies to primary rays; secondary paths have unrestricted distance. Misses return black. SplitMix64 shader arithmetic uses core uvec2 operations to match the CPU IndependentSampler for 64-bit seed/sample counters. Numerical operations can still differ between CPU and GPU.

The runtime path is CPU scene compilation/upload -> OpenGL compute ray generation/BVH traversal/transport -> GPU HDR accumulation -> shared texture -> display shader exposure/Reinhard/sRGB -> debug overlays. There is no per-round image readback, CPU Film averaging, or image upload in this path. Eight std430 buffers hold nodes, order, unique-mesh triangles, instances, materials, emitters, emitter lookup and a numerical-error counter. Only that four-byte status counter is read after completion. A 1 ms fence poll bounds cancellation response and keeps dispatches from building an unbounded command queue; an already submitted GPU dispatch still runs to completion.

Three pinned presentation textures keep published frames immutable. Producer fences/flush and display waits import shared-context writes; consumer fences prevent reuse while a previous draw still samples a texture. GLFW creation/destruction stays on the window thread. Compute has a shared hidden OpenGL 4.3 core context; presentation retains OpenGL 3.3. Exposure is applied at display time and preserves accumulated HDR samples. Resolution, scene/camera changes and transport changes start fresh accumulation. Normal output bypasses light transport and color conversion.

Headless ComputeRayTracer construction defaults to GPU PathTrace and uses an independent compute context. Explicit settings must select RayDevice::GPU. Its public render(scene, request, film) intentionally reads one sample into CPU Film for export/testing; use film.resolve(exposure, renderer.imageEncoding()) for the chosen output. Runtime handles shared GPU presentation automatically.

This is programmable OpenGL compute path tracing; it uses no Vulkan/DXR/CUDA dependency or hardware RT pipeline. Use native drivers (JADE_USE_SOFTWARE_RENDERING=OFF) for GPU execution; Mesa llvmpipe executes compute on CPU. Unsupported compute contexts fail explicitly. BLAS/TLAS building, refit and emitter distributions remain on CPU and reuse the existing caches. Textures, volumes, nested media, denoising and dedicated caustic algorithms remain outside both implementations.

## Collision detection

Collider is independent of render geometry. Box stores local half-extents and Sphere stores a local radius; both are centered at the Entity's origin. Transform supplies the full hierarchy position, rotation and scale. Include Jade/Physics/Collider.hpp and Jade/Physics/Collision.hpp, or Jade/Jade.hpp:

```cpp
auto box = scene->create();
box.add<Jade::Transform>();
box.add<Jade::Collider>(Jade::Box{{1.0f, 2.0f, 1.0f}});

auto sphere = scene->create();
sphere.add<Jade::Transform>().position = {1.5f, 0, 0};
sphere.add<Jade::Collider>(Jade::Sphere{1.0f});

if (auto hit = Jade::Physics::contact(*scene, box, sphere)) {
  // hit->point: representative world contact point
  // hit->normal: unit separation direction for hit->b
  // hit->penetration: separation distance; zero for touching
}
auto contacts = Jade::Physics::detect(*scene);
```

Queries automatically derive world shapes and AABBs from current components. AABB overlap only selects candidates; precise detection uses Sphere–Sphere, Sphere–Box and rotated Box–Box. No Auto shape or AABB component is required. Box dimensions are half-extents: {1, 2, 1} describes a box of size {2, 4, 2}. Box supports nonuniform world scale, provided its world basis remains orthogonal. Sphere requires uniform world scale; nonuniform scale and hierarchy shear throw std::invalid_argument. Dimensions must be finite and positive. Scale/orthogonality checks allow a 1e-5 relative rounding tolerance. Geometric queries do not simulate; Physics::System separately provides gravity and collision response.

These main-thread queries need no window, camera, mesh or material. detect returns one Contact per pair in ascending Entity-ID order and skips Colliders without Transform. contact returns no hit for missing Collider/Transform or self-pairs, and rejects foreign/dead entities. It preserves the supplied entity order; reversing the pair reverses its normal. The point is a midpoint of surface witnesses, including a representative of clipped face contacts. Normal remains a separation direction during containment, rather than necessarily pointing between centers. Touching and rounding-sized gaps count as zero-penetration contacts, using a distance tolerance of 1e-6 times max(1, the pair's largest world half-extent or radius).

Collider stores a copy of the supplied shape. Shape data needs no separate allocation and its storage is private. Replace it with `entity.get<Jade::Collider>() = Jade::Collider{Jade::Sphere{2}}`; the next query observes the change. Calling detect is explicit; an application may call it in FrameUpdate after moving entities. Queries do not move entities or emit collision events.

```cpp
config.debug.physics = true; // AABBs, shapes, contact points/normals and logging.
// Or change them while running:
app.runtime().setPhysicsDebug(true);
```

With config.debug.shortcuts=true, F3 toggles axes/cyan AABBs and F4 independently toggles orange shapes, red contact markers, green normals, blue field force, yellow total force and Physics/Debug logging. Overlays work over image/Wireframe or a cleared background and stay outside path tracing snapshots. Physics/Debug reports environment, body, contact and force information on activation and at most once per second; Trace adds contact witnesses and solved impulses. Dynamics retains contact impulses between fixed steps and Broad Phase uses sweep-and-prune.

## Scene modules and controls

There is one camera controller: `Module::FPSCameraControl`. Demo and the F2 observer use the same upright navigation model. Camera pose, lens projection, interaction mode and input bindings have separate responsibilities.

```cpp
#include <Jade/Module/FPSCameraControl.hpp>
#include <Jade/Graphics/CameraControl.hpp>

scene->use<Jade::Module::FPSCameraControl>();

// Optional per-camera settings. Without this component, editor defaults apply.
auto& controls = scene->camera().add<Jade::CameraControl>();
controls.viewMode = Jade::ViewMode::Editor;
controls.speed = 3;
controls.sensitivity = 0.1;
controls.bindings.look = Jade::MouseButton::Right;
controls.bindings.pan = Jade::MouseButton::Middle;

// Continuous captured mouse look uses the SAME controller and movement model:
controls.viewMode = Jade::ViewMode::FirstPerson;
// The independent F2 observer has its own settings:
app.runtime().observerControls().sensitivity = 0.15;
```

| Input | Default behavior |
| --- | --- |
| WASD / arrows | Horizontal movement, 3 units/second |
| Space | Ascend along parent Y |
| Left / right Shift | Descend along parent Y; combines with WASD |
| Ctrl + movement | Slow movement, 1 unit/second, in every direction |
| Right-button drag | Look left/right/up/down, 0.1 degrees per logical pixel |
| Middle-button drag | Grab-style image-plane pan in either projection |
| Wheel | Zoom the active lens |
| Q / E | Unbound |

Yaw turns around fixed parent Y (world Y for root cameras); pitch is clamped to +/-89 degrees and roll is removed. Looking up/down never changes horizontal speed. Combined movement is normalized, opposing inputs cancel, and pressing both WASD and its arrow alias does not double the speed. This is a navigation controller; character jumping, gravity and collision belong to gameplay/physics.

`ViewMode` describes interaction: Editor keeps the cursor free and uses button gestures; FirstPerson captures it for continuous look. Both support the same movement, pitch constraints and wheel zoom. Projection remains `Camera::projection`, independent of ViewMode. Orthographic pan scales by viewHeight/windowHeight; perspective pan scales at `CameraControl::panDistance` using FOV/windowHeight. Pan preserves orientation and follows the current zoom and logical window size, including high-DPI cursor coordinates.

Settings and bindings belong to the camera entity and can change without reinstalling the module. `bindings[CameraAction]` accepts two physical keys per action; `Key::Unknown`/`MouseButton::Unknown` disable bindings. Shift/Ctrl do not suppress held movement, while Alt/Super combinations remain shortcuts. Typed `KeyHeld<"C-w">` subscriptions still require exact modifiers. Global consumption suppresses corresponding scene camera input.

The module resolves `scene->camera()` for each event, supports installation before a camera exists, skips missing components, and installs once per Scene. Camera/scene/settings/projection changes and focus loss cancel active gestures. F2 suspends scene camera navigation and routes movement, look, pan and wheel to the observer; gameplay updates continue. Exiting F2 restores scene controls without resuming an old drag. Scene destruction and failed module installation release subscriptions, including nested camera control state.

`Graphics/Motion.hpp` remains general Transform math, including unrestricted local-axis rotation. `Graphics/CameraMotion.hpp` provides upright camera look, horizontal/vertical movement and projection-aware pan without input dependencies. The old FreeCameraControl module is removed; replace its include/use with FPSCameraControl.

Any type providing `static void attach(Jade::Scene&)` can be a module; no instance is constructed:

```cpp
struct Spin {
  static void attach(Jade::Scene& scene)
  {
    scene.on<Jade::FrameUpdate>([&scene](const auto& frame) {
      for (auto entity : scene.query<Jade::Transform>()) {
        Jade::Motion::yaw(entity.get<Jade::Transform>(), 15 * frame.dt);
      }
    });
  }
};
scene->use<Spin>();
```

`use<T>()` installs once per type per Scene; repeated calls do nothing, including after reactivation. Recursive installation of the same type throws `std::logic_error`. If attach throws, new scene subscriptions are cancelled and new nested module installation records are removed, permitting retry; pre-existing subscriptions and modules remain. Other side effects, such as component changes or external registrations, are the module's responsibility. There is no module unload API; callbacks live with Scene.

Bare F1 (mesh wireframe) and I (Raster/Hybrid/RayTracing/None cycle) always work once per unconsumed press. `EngineConfig::debug.shortcuts` defaults to false; enabling it permits F2 (soul observation camera), F3 (scene axes/AABBs/BVH/frustums/lights), F4 (physics/forces/logs), F5 (independent normal debug) and O (camera projection). Global and scene callbacks run first; Stop, app.stop() or a closed window suppresses defaults. Initial visibility follows config.debug.axes/aabbs/physics independently. Runtime exposes setAxes(), setAabbs() and setPhysicsDebug(); this flag does not change the process-wide logger threshold.

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
  app.runtime().setScene(otherScene);
  return Jade::EventFlow::Stop;
});
scene->on<Jade::SceneDeactivated>([&] { dragging = false; });
scene->on<Jade::SceneActivated>([&] { dragging = false; });
```

`app.on` handles global events first; the active scene's `on` handles them next, followed by unconsumed Escape/F1/I and enabled F2/F3/F4/F5/O defaults. Inactive scenes retain state and subscriptions but receive no input/update events. During Run, selection is queued until the next processing pass; the current pass uses one retained scene for input, update and rendering. The last request wins. Outside Run it commits immediately. Selecting the current scene emits no lifecycle events.

Deactivation runs before committing the pointer; activation runs after. Lifecycle Stop ends listener propagation but does not cancel the switch. Reentrant selections queue another switch. A deactivation exception retains the old scene; an activation exception retains the new scene. Both discard pending requests and propagate the original exception. Application destruction releases ownership without calling lifecycle callbacks. One scene can be active/reserved in only one Application at a time.

The first frame after switching has dt=0; elapsed and submitted frame indices continue through the Run. Physical held input persists, without synthetic Pressed/Released events. Reset transient state such as dragging in lifecycle handlers. See [Events](docs/Events.md) for lifetime and timing details.

## Public headers

Public interfaces are split by purpose, with one optional includes-only entry point, Jade.hpp:

| Folder/header | Interface |
| --- | --- |
| Core/ | Scene, SceneView, Entity, Transform |
| Math/ | Vector (Vec2/Vec3), Quaternion, Matrix4 |
| Graphics/ | Mesh, Vertex, Geometry, Camera, Motion |
| Materials/ | Material parameters, validation and diffuse/metal/glass/emissive presets |
| Renderer/Common/ | RenderSettings, RenderProgress, SceneSnapshot, camera, HDR Film and Image |
| Renderer/RayTracing/ | ProgressiveBackend, RayTracing, RayScene/Compiler, RayQuery, Integrator, BSDF and Sampler |
| Assets/ | Assets, AssetHandle and MeshHandle |
| Animation/, Graphics/Texture.hpp, Graphics/SpriteRenderer.hpp | Timed sprite clips, Animator, Animation::System, PNG textures and SpriteRenderer |
| Physics/CharacterController.hpp | Planar character movement and static-collider sliding without RigidBody |
| Input/, Events/ | Key/button bindings and event contracts |
| Application.hpp, Color.hpp, Log.hpp, WindowConfig.hpp | Application loop and value/configuration types |

There are no folder aggregation headers. Source `.cpp` files include their matching private `.hpp` first and are explicitly listed in CMake. Registry storage, ProgressiveSession, OpenGL display, OBJ parsing, input state, event dispatch, matrix construction and clock are internal. Consumers receive only `Jade/include`.

## Frame scheduling and logs

`config.frameLimits` defaults to 24 frames per second and must be positive. The loop uses monotonic absolute deadlines and waits for events between frames; slow frames skip missed slots. config.renderer.vsync defaults to false. `runtime.renderer().setRayTracingEnabled(false)` bypasses ray computation. Wireframe, axes and physics overlays remain independent of it. Showcase starts in None with wireframe on and axes disabled; Untitled starts in Raster with engine debug shortcuts enabled.

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

`EngineConfig::logPerformance` defaults to true. A completed one-second
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

GLAD 2.0.8 OpenGL 3.3 Core output with zero extensions is checked into `vendor/glad/`. Configuration compiles its C source directly and performs no GLAD download or generation. `cmake/EmbedShaders.cmake` embeds GLSL during the build using CMake alone.

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
Jade/src/Renderer/   # Common/ snapshots/images and RayTracing/ transport
Jade/src/internal/Renderer/Pipeline/ # Shared render passes and built-in pipelines
Jade/src/internal/Renderer/Device/OpenGL/ # Raster, compute, presentation and line execution
Jade/src/internal/   # Registry, ProgressiveSession, display/line shaders, platform and input
Demo/               # Showroom, three-body orbit, pyramid/drop and destruction wall
assets/             # Bunny OBJ and attribution
tests/              # ECS, assets, math, input, scheduling and pixel checks
```

GoogleTest exercises identity generations, ownership, hierarchy, scene switching, component/query mutation, shared immutable resources, OBJ loading, quaternion controls, projections, resize, ray intersections, BSDFs, emissive lighting, deterministic CPU sampling, asynchronous revisions, wireframe, axes and events; OpenGL readback validates presentation/debug output. Tests build independently of Demo. See [Architecture](docs/Architecture.md) for boundaries and [Validation](docs/Validation.md) for recorded checks.

Interactive wheel zoom scales the image-plane span, preserves pose and clip planes, and limits perspective zoom to 1?120 degrees. Camera navigation retains scroll beyond the limits so reversing a gesture restores its original projection, including orthographic height. Direct lens edits establish a new zoom reference.
