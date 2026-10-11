# Windows x64 validation

## Runtime, Archetype and rigid-body dynamics (2026-10-10)

Debug and Release each passed 197/197 GoogleTest tests across 46 suites. Both isolated configurations rebuilt the engine, Demo and tests without compiler warnings. Nine affected public headers passed standalone MSVC C++20 /W4 /WX compilation. Runtime now owns Scene selection, Assets, Renderer::System and Physics::System; tests retain coverage of scene switching, callback/worker exceptions, recovery, input propagation and rendering.

Archetype checks cover recursive flattening/deduplication, typed creation, manual component composition, empty-signature queries across destruction/recycling, incomplete/throwing constructor rollback and stable component references. A signature-query fixture includes 5,000 unrelated entities and verifies that only matching group rows are visited. Physics checks cover gravity, mass-dependent linear/off-center angular impulses, four-point Box face manifolds, Sphere contacts, restitution, friction, static bodies, invalid-input rollback and recycled identities. Runtime checks compare 30/60-FPS clocks, substep limits, dropped debt, pause/resume, scene changes and repeated Run boundaries.

The 5/3/1 pyramid fixture advances nine boxes and a static floor for 2,400 steps at 1/120 second with gravity continuously enabled and no sleeping. The last 600 steps produce the same rounded measurements in Debug and Release:

| Measurement over the final five seconds | Observed maximum | Acceptance limit |
| --- | --- | --- |
| Linear speed | 0.000001 m/s | < 0.02 m/s |
| Angular speed | 0.000000 rad/s (rounded) | < 0.02 rad/s |
| Penetration | 0.003336 m | <= 0.005 m |
| Position drift | 0.000303 m | < 0.01 m |

Actual OpenGL readback verifies cyan AABBs, yellow shapes, red contact markers and green normal arrows. Logging checks verify activation and summary throttling. Both actual Demo executables completed 240 frames with normal shutdown and empty stderr. The Debug launch exceeded its frame budget and exercised substep-debt dropping; these launches establish successful execution, not realtime performance. An ignored Release harness built from the actual Demo source with a hidden window invokes real GLFW callbacks and checks F4, P, K, 1/2/3, R and I: unified debug state, pause preservation, angular impulse, scene selection, reset and transitions between Wireframe/RayTracing all passed.

Artifacts are ignored under .temp/runtime-debug-tests.xml/.log, .temp/runtime-release-tests.xml/.log, .temp/runtime-*-build.log, .temp/runtime-*-demo.log/.err and .temp/runtime-demo-input.log. Builds are build/collision-debug/ and build/collision-release/. Physics remains discrete and single-threaded, with all-pairs broad phase; these checks do not establish CCD, arbitrary hierarchy dynamics or large-world scalability. No commit was created.

## F4 Collider debug shortcut (2026-10-10)

Debug and Release each passed all 14 ApplicationDebug, CollisionDebug and RenderMode tests. Existing keyboard callback checks now cover F4 opt-in, repeats, modifiers, event consumption and independent wireframe/axis/AABB state. OpenGL readback verifies yellow Collider shapes and cyan AABBs alongside actual gray-white mesh wireframe. Both isolated configurations rebuilt the engine, Demo and tests successfully. Demo cubes and spheres now carry matching Colliders. No commit was created.

## Collider v1 (2026-10-10)

Debug and Release GoogleTest both passed 183/183 tests across 43 suites. Sixteen added Collider/Collision/CollisionDebug tests cover value copying and shape replacement; Sphere–Sphere, Sphere–Box and rotated Box–Box contacts; containment and coincident centers; symmetry and stable pair ordering; AABB false positives; skew edge contacts; hierarchy bounds and scale rules; invalid dimensions, shear, missing components and dead/foreign entities. A deterministic 250-pair rotated-box fixture guarantees intersection by construction and verifies that the reported normal/penetration separates the pair. A large-coordinate regression verifies that Sphere–Box normal calculation preserves the local closest displacement when absolute world witnesses round together.

Actual OpenGL readback verifies independent yellow shape and cyan AABB overlays over both image and wireframe presentation, including Collider-only entities. Application tests verify opt-in validation, runtime flags and unchanged path tracing revisions after Collider-only edits, shape removal and display toggles. Existing camera, input, rendering and Scene regressions remain included in the full runs.

Isolated build/collision-debug and build/collision-release configurations built the engine, Demo and tests successfully without compiler warnings. The two new public Physics headers and Jade.hpp passed standalone MSVC C++20 /W4 /WX compilation. All eight new engine files and the new test source are explicitly listed in CMake, and git diff --check passed. No commit was created. The implementation provides explicit discrete collision queries, automatic world AABB screening and one representative Contact per pair; rigid bodies, physical response, continuous detection and spatial-index performance are outside this validation.

## Camera control split (2026-10-10)

Debug and Release GoogleTest both passed 167/167 tests across 40 suites after replacing CameraControl with FreeCameraControl and FPSCameraControl. The added checks cover mutually exclusive installation and dependency rollback, horizontal movement at full speed while pitched, continuous mouse look and +/-89-degree pitch limits, absent roll/flight bindings, stationary quaternion stability, external pose edits, camera replacement/removal, cursor capture, scene/focus transitions and subscription ownership. Free flight and existing application/input/rendering regressions also passed.

The new public module headers and Jade.hpp passed standalone MSVC C++20 /W4 /WX compilation. Release engine, Demo and tests built successfully. Debug engine and tests built successfully; an already running JadeDemo.exe prevented overwriting its output, so the updated Debug Demo was linked separately as build/windows-debug/bin/JadeCameraControlDemo.exe. The running process was retained. These checks do not implement or validate jumping, gravity or collision.

Validated on 2026-10-10 with MSVC 19.51, C++20, CMake/Ninja and Mesa 26.2.4 llvmpipe for window/display checks. Path tracing runs on CPU workers and needs no graphics context.

| Check | Result |
| --- | --- |
| Windows Debug engine, Demo and tests build | Passed without compiler warnings |
| Windows Release engine, Demo and tests build | Passed without compiler warnings |
| Debug GoogleTest | 161/161 passed, 38 suites |
| Release GoogleTest | 161/161 passed, 38 suites |
| Standalone public headers | All 44 passed MSVC C++20 /W4 /WX |
| External CMake consumer | Built with Demo/tests disabled, linking only Jade::Jade |
| Headless consumer render | 640x424, 256 SPP, 5 instances, 12 emissive triangles |
| Debug and Release Demo launches | 240 frames each, normal frame-limit shutdown and empty stderr |
| Actual input/mode integration | 240 frames with repeated I toggles and a large reversible wheel gesture |
| Source/header/shader inventory and first includes | All 135 maintained files explicitly listed; corresponding header included first |
| GLSL inventory | Four presentation/line shaders, with explicit stage names and PascalCase filenames |
| Git whitespace check | Passed |

## Physical rendering

RendererTests exercises headless geometric and transport checks. Nearest hits, clipping, back faces, shared resources and entity identities are checked directly. Instance transforms under nonuniform scale and inherited shear agree with baked world geometry. SAH BVH traversal agrees with an independent brute-force triangle oracle. Detached snapshots retain meshes beyond Assets lifetime and ignore later component edits. Degenerate triangles, missing materials, foreign handles and invalid/projective transforms have defined outcomes. Cancelled compilation can be retried.

SahTests verifies independent Material preset values and generic component composition, synchronous MeshData compilation before asset publication, immutable shared BLAS ownership, OBJ cache reuse and original triangle identities after excluding degenerate triangles. TLAS checks cover camera/material reuse, transform refit, quality-triggered SAH rebuilding, layout/mesh changes and published snapshot isolation. Emitter distribution checks distinguish ordinary instance changes from emission, sidedness and emitting-transform changes. Cancelled or failed scene compilation preserves the last successful compiler state. Private BVH tests cover near-first pruning, shadow early exit, coincident centroids, the 48-level depth cap, extreme finite areas and transactional cancellation.

An independent median-split reference is used only in tests. In the nonuniform fixture (48 small clustered bounds and one distant larger bound), estimated SAH cost was 1.015094 versus 7.972450 and candidate primitive tests were 1 versus 4 for the selected ray. An earlier local fixture measurement recorded a 30.1-microsecond Release build and 405.5-microsecond Debug build under concurrent load. Timing is recorded without a pass/fail threshold. These fixture measurements do not establish a general rendering-speed improvement.

Camera tests cover perspective/orthographic ray generation, aspect ratio and near/far planes. Secondary transport beyond the primary far plane remains visible. Diffuse sampling recovers reflectance; smooth/rough metal and dielectric sampling agree with their PDFs. Glass tests cover refraction, total internal reflection and the IOR=1 limit. No-emitter scenes return black; one-sided emission and HDR values are preserved. Independent checks cover emitter tessellation invariance, shadows and indirect red-wall color bleeding.

Film checks cover HDR running means, invalid radiance rejection, exposure/tone mapping and sRGB encoding. Identical seeds/sample counts produce identical images with one and three workers. Cancellation commits no partial sample round. Custom integrators work headlessly and cannot be replaced while an Application session is active.

## Scheduling and wireframe

Controlled ProgressiveSession tests verify camera-revision coalescing, cancellation during resize/invalidation, rejection of stale images and worker-exception propagation. Application owns its RayTracing from construction. Integrator configuration is locked during Run, including wireframe, and is unlocked after normal shutdown or failure. A throwing worker test synchronizes on completion of the failure before checking propagation and recovery.

RenderMode tests invoke real GLFW callbacks. I works with debug disabled, respects event consumption, modifiers and repeats, and toggles RayTracing/Wireframe. Debug F retains the same shortcut. Starting Run in wireframe invokes no integrator; entering wireframe cancels and joins work, and returning starts fresh samples of the current scene. Window titles and RenderProgress identify the visible mode; wireframe reports no physical samples. A 240-frame integration exercises the real path tracer, repeated I toggles and reversible wheel zoom.

The wheel regression drives perspective and orthographic lenses beyond their interactive bounds and reverses the same gesture, restoring the initial lens and pose. Zoom scales tan(FOV/2), limits interactive perspective zoom to 120 degrees and retains the accumulated gesture. The original angular scaling could reach 179 degrees and discard scroll at the clamp.

Real OpenGL readback verifies sRGB image orientation and bytes, unlit triangle edges and unit RGB axes. Existing Scene, hierarchy, Assets, mesh/OBJ, math, FreeCameraControl, module rollback, event dispatch, input ordering, timing and logger suites remain maintained. Automated callback/readback checks do not establish manual keyboard/mouse behavior or realtime rendering performance.

## Demo and exported output

Both Demo executables were launched from their actual bin directories using --frames 240. Logs confirm bunny loading, llvmpipe presentation, completed RayTracing samples at 480x320 and normal frame-limit shutdown. Demo opens a 960x640 window and uses resolutionScale 0.5; the engine default remains 1. These launches ran under concurrent validation load. Input/presentation pacing and physical sample throughput are independent, and neither these launches nor the IDE debug button establish a performance guarantee. Manual controls and the IDE debug button were not exercised.

The standalone-header check exposes only Jade/include. The external CMake consumer rejects leaked GLFW/GLAD include paths at compile time. It captures SceneSnapshot, compiles RayScene, accumulates Film and exports RGBA8 without constructing Application or opening a window. Its 640x424, 256-SPP output was visually inspected: diffuse bunny, gold cube, glass sphere, floor and emissive ceiling geometry are present.

Physical light transport, reflection and refraction run through the CPU Integrator. OpenGL presents resolved sRGB images and draws diagnostic lines. The four embedded GLSL stages are Display.vert, ImageDisplay.frag, Lines.vert and Lines.frag. Low-sample noise remains expected; textures, volumes, nested media, denoising and a dedicated caustic algorithm remain outside this version.

Validation artifacts remain ignored: .temp/ray-only-debug-tests.xml/.log, .temp/ray-only-release-tests.xml/.log, .temp/ray-only-header-checks.log, .temp/ray-only-consumer-build.log, .temp/ray-only-consumer.log and .temp/ray-only-consumer.bmp. Demo logs are .temp/ray-only-debug-demo.log/.err and .temp/ray-only-release-demo.log/.err. Primary builds use build/windows-debug/ and build/windows-release/; the independent consumer is build/ray-consumer/. GLAD compiles from checked-in C under vendor/glad and shader embedding uses CMake. Configuration, compilation and validation require no Python tooling.
