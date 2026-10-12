# Jade

Jade is a C++20 World Runtime and rendering library for Windows x64 / MSVC. A World owns entity lifetimes, hierarchy, streaming and simulation state. ECS stores components in stable paged pools. Scene selects a World, camera and input context. Runtime schedules Worlds independently of the active Scene and coordinates Assets, jobs, animation, physics and rendering.

The ownership API has changed deliberately: create entities through `World` or `world.registry()`. Construct a Scene with an explicit `shared_ptr<World>`. There are no Scene entity-management forwarding methods.

## Build and run

Use an x64 Developer PowerShell with MSVC, Windows SDK, CMake and Ninja installed:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel
& ./build/windows-debug/bin/JadeSmokeTests.exe
& ./build/windows-debug/bin/Untitled.exe

cmake --preset windows-release
cmake --build --preset windows-release --parallel
& ./build/windows-release/bin/JadeSmokeTests.exe
& ./build/windows-release/bin/JadeEcsBenchmark.exe
```

GLFW and GoogleTest are pinned CMake dependencies. GLAD and nlohmann JSON 3.12.0 are vendored; JSON remains an implementation dependency. PNG loading uses Windows WIC. No Python tools are required. `JADE_BUILD_DEMO`, `BUILD_TESTING` and `JADE_BUILD_BENCHMARKS` independently control applications, tests and headless benchmarks. GoogleTest runs directly; no CTest entry point is provided.

Visual Studio discovers the checked-in presets. VS Code's CMake Tools integration supplies configure/build/debug tasks for JadeDemo, Untitled and JadeSmokeTests. Outputs are under `build/windows-{debug,release}/bin`; each application finds its assets relative to its executable. Machine-specific settings belong in ignored `CMakeUserPresets.json`.

## World and Scene

```cpp
#include <Jade/Jade.hpp>

Jade::Application app;
auto world = std::make_shared<Jade::World>();
auto scene = std::make_shared<Jade::Scene>(world);
app.runtime().addWorld(world);

auto camera = world->create<Jade::Archetype<Jade::Transform, Jade::Camera>>();
camera.get<Jade::Transform>().position = {4, 3, 8};
Jade::Motion::lookAt(camera.get<Jade::Transform>(), {});
scene->setCamera(camera);
scene->use<Jade::Module::FPSCameraControl>();

auto ball = world->create<Jade::Archetype<Jade::Transform, Jade::Geometry,
  Jade::Material, Jade::Collider, Jade::RigidBody>>();
ball.get<Jade::Transform>().position = {0, 2, 0};
ball.get<Jade::Geometry>() = Jade::Geometry::sphere(0.5f);
ball.get<Jade::Material>() = Jade::Materials::Gold();
ball.get<Jade::Collider>() = Jade::Collider{Jade::Sphere{0.5f}};

app.runtime().setScene(scene);
app.Run();
```

An explicitly scheduled World continues updating with no active Scene. An active Scene's World is also scheduled implicitly; Runtime deduplicates the two paths. Multiple Scenes can reference one World. Switching those views retains bodies, contacts and simulation state. Removing a World requires detaching its active Scene and saves its state before releasing Runtime ownership.

`world.setParent(child, parent)`, `parent`, `children`, `worldMatrix` and subtree `destroy` own hierarchy policy. Transform stores local TRS; full matrices retain inherited shear. Physics rejects unsupported collider shear and parented dynamic bodies. Streaming roots cannot be parented, and children cannot cross Chunk ownership boundaries.

## ECS access

Components are ordinary C++ types. `Archetype<T...>` is a type-only descriptor, recursively flattened and deduplicated. It does not select an archetype column-store backend or construct a descriptor instance.

```cpp
struct Velocity { Jade::Vec3 value; };
auto entity = world->create<Jade::Archetype<Jade::Transform, Velocity>>();
entity.get<Velocity>().value = {1, 0, 0};

world->registry().each<Jade::Transform, Velocity>(
  [&](Jade::Entity e, Jade::Transform& pose, Velocity& velocity) {
    pose.position = pose.position + velocity.value * 0.01f;
    world->registry().markChanged(e);
    if (pose.position.x > 100)
      world->command([e](Jade::World& w) { w.destroy(e); });
  });
world->commit();
```

`each` traverses cached matching signature groups without copying or sorting entity identities. Structural changes during traversal throw; queue World commands and commit at a boundary. Traversal order is unspecified. `query<T...>()` creates a sorted identity snapshot: it skips destroyed entities and removed matches, and defers new matches until the next query. Keep the view alive while using its iterators.

Component references survive unrelated creation, signature changes and pool growth. Removal, entity destruction and Registry destruction invalidate them. Generation-tagged handles reject reused slots. Handles and views do not retain World lifetime. Duplicate add and missing get throw; absent remove is a no-op. Constructors may edit other components; recursive construction of the same component is rejected. Subtree identities are invalidated before component destructors run, with children retired before parents.

`registry.patch<T>(entity, callback)` records a revision after a value edit. Direct `get<T>()` remains available; call `markChanged` when a consumer needs that revision. Current rendering and persistence capture actual component values, so correctness does not depend on callers marking raw edits.

## Streaming and persistence

`ChunkProvider` supplies immutable CPU data. `ObjectCodec` converts versioned object JSON into a supplied entity root and captures persistent state. `WorldStore` persists whole-world overlays; runtime Entity IDs and resource pointers never cross the I/O boundary.

Demand has independent data, render, collision and simulation radii. Default radii are 2/2/2/1: 25 data/render/static-collision Chunks and 9 simulated Chunks away from world edges. Dynamic objects outside simulation demand retain pose and velocity but stop colliding and simulating. CPU-only prefetch creates no entities. Multiple sources combine their demands.

Loads use the shared Asset job category, with at most four accepted loads and two main-thread commits per update. Unload hysteresis defaults to one second; at most two Chunks release per update. Acknowledged saves precede persistent entity release. Load/save failures retain diagnostic state and support explicit retry. `streaming.barrier()` is an explicit startup/teleport wait, outside normal frame updates.

Stable `ObjectRef{WorldId, ObjectId}` resolves only resident objects. Migration preserves identity and transfers ownership; unloaded destinations are pinned until ready. Deleted objects leave persistent tombstones. FileWorldStore writes a flushed immutable JSON snapshot and atomically replaces `commit.json`, keeping the current and previous generations. Normal Runtime shutdown saves; destructors do not silently save. A save directory has one owning writer.

See [World runtime contracts](docs/WorldRuntime.md), [architecture](docs/Architecture.md) and [Untitled](Demo/Untitled/README.md) for lifecycle, API and demo details.

## Geometry, physics and rendering

`Geometry::mesh(handle)`, `Geometry::sphere(radius)` and `Geometry::plane(size)` share Material and Transform. Sphere and finite XZ-plane intersections are analytic in CPU/GPU rays and Hybrid shadow rays, including nonuniform scale for ellipsoids. Raster, wireframe and debug drawing use cached tessellation. Analytic emitter sampling accounts for the transformed surface-area Jacobian; collision remains an independent Collider choice.

Renderer selects Raster, Hybrid, RayTracing or None. Hybrid traces visibility during direct raster lighting; RayTracing provides CPU/GPU diffuse, metal, dielectric and emissive transport. Raster/Hybrid have up to eight approximate direct lights and ambient lighting, with opaque tinted dielectric shading. Reflection, refraction and indirect light remain tracer features. F1 wireframe, F3 bounds/axes, F4 physics and F5 normals are independent overlays. F2 enables a separate observation camera without replacing the game camera.

World owns its native `Physics::World`, ECS body bindings, fields and fixed-clock remainder. `Physics::System` is the ECS bridge; standalone native physics requires no Scene, window or GPU. Runtime defaults to 1/120-second steps, up to eight per frame. Gameplay runs at a World fixed boundary before physics; animation follows before snapshot capture. Excess whole-step debt is discarded. See [physics](docs/Physics.md) for the native body API, CCD, solver and benchmark contracts.

Runtime owns immutable Assets, categorized jobs and render services. Workers read immutable snapshots or private simulation buffers; Registry mutation and codec spawn/capture run on the owning thread. Scene-local event modules and Application-global events retain ordered dispatch and consumption semantics; see [events](docs/Events.md).

## Applications and validation

Untitled is the World Streaming showcase: 16x16 authored Chunks, 16 metres each, with 4x4 grass cells. WASD moves the character; Q/E rotates the camera; F pushes a nearby ball; X deletes it; R resets the player. Its HUD shows streaming demand and save status. Changes to persistent balls survive unload/reload and process restart.

JadeDemo preserves the five Showcase scenes: materials, orbit, pyramid, destruction wall and 100,000-body stress test. Scene selection uses 1–5; G changes gravity where supported, K applies impulses, P pauses and R rebuilds. Scenes have separate Worlds and retain state while inactive. The stress scene has no realtime performance guarantee.

Automated regressions cover ownership, paged storage, streaming/persistence, physics, CPU/GPU rendering, actual OpenGL readback, animation, events and input. [Validation](docs/Validation.md) records measured results and the limits of those checks. Maintained source uses two-space indentation.
