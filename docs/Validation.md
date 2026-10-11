# Windows x64 validation

## Archetype descriptors and empty resources (2026-10-11)

Debug and Release each passed 235/235 GoogleTest tests across 49 suites. The final Debug run took 263.577 seconds and Release took 26.499 seconds. The engine and tests built in both configurations without compiler warnings. Release Demo built normally. The running Debug JadeDemo.exe prevented replacing its executable, so the compiled Demo object was linked separately as build/windows-debug/bin/JadeArchetypeDemo.exe.

Archetype coverage verifies alias and named Components descriptors, recursive composition and first-appearance deduplication, a deleted descriptor constructor that is never instantiated, Entity return types, explicit Transform presence, component defaults, component-constructor rollback, and rollback when a constructor removes an earlier declared component. Existing signature indexing, snapshot queries, entity generations and stable component references remain covered. Renderer-mode checks await completed sampling while Run retains the coordinator instead of assuming a fixed frame count guarantees worker completion.

Empty-resource regressions verify Geometry mesh binding/clearing and rejection of nonempty expired handles. Collider checks cover the constexpr empty state, copying and shape replacement, omission from contact/detect/manifold/debug queries before unused transform validation, and exclusion from physics logs. Serial, one-worker and three-worker simulations verify inactive bodies retain pose/velocities, empty-body graphs succeed, applyImpulse rejects empty shapes without mutation, binding activates simulation, and clearing removes current contacts and subsequent solver state. Invalid participating body/shape data retains existing transactional validation.

Eight affected public headers passed standalone MSVC C++20 /W4 /WX /permissive- compilation. Four compile-rejection checks verify non-default-constructible components, raw component types passed to create<T>(), malformed Components declarations, and the removed constructor-argument creation API. An external CMake consumer with Demo/tests disabled linked only Jade::Jade and ran headlessly, checking both descriptor forms, post-creation resources, physics activation/suspension, dynamic components, and empty-signature queries. Debug and Release Demo each completed two-frame launches for scenes 1 through 4 with exit code zero and empty stderr. Git diff --check passed.

Artifacts are ignored under build/validation/archetype-debug-final.xml/.log, archetype-release-final.xml/.log, archetype-public/, archetype-consumer-source/, archetype-consumer-build/, and archetype-windows-debug-scene1 through scene4 / archetype-windows-release-scene1 through scene4 logs. The public-header, compile-rejection and Demo launch helpers are build/validation/archetype-public.ps1 and archetype-demo-smoke.ps1.

## Scene force fields, three-body orbit and force overlays (2026-10-11)

Debug and Release each passed 235/235 GoogleTest tests across 49 suites. Engine, Demo and tests built successfully without compiler warnings in isolated build/fields-debug and build/fields-release configurations. Eight public headers (ForceField, GravityField, Environment, System, Scene, EngineConfig, Runtime and Jade.hpp) passed standalone MSVC C++20 /W4 /WX compilation. The initial primary Debug build encountered an occupied renderer object; the isolated Debug build uses /Z7 embedded compiler debug information after an MSVC shared-PDB write failure. Repository presets are unchanged.

Thirteen Fields tests verify mass-dependent force and mass-independent acceleration, scene-local Earth/Moon/empty environments, softened point gravity and composition, source validation, self exclusion and recycled identities, transactional failures at either Verlet stage, average applied force, support/friction/warm-start force accounting, stale snapshot invalidation, second-order orbital convergence and serial/worker equivalence. The three-body fixture advances 100 outer orbital periods at 1/120 second using masses 100/10/0.1, distances 8/0.9, G=1 and softening=0.001. Maximum relative energy error is approximately 0.000031 (0.0031%); normalized momentum drift is approximately 0.000006, below limits of 0.001 and 0.0001. The Moon remains within its bounded orbit. Existing 20-second stack and persistent-anchor regressions also pass.

OpenGL back-buffer readback verifies distinct blue field-force and yellow total-force arrow pixels alongside updated orange Collider outlines, red contacts, green normals and cyan AABBs. Force geometry tests verify direction/length and suppression of balanced support force. Real input adapter tests cover grouped F3 axes/AABB controls, independent F4 physics controls, opt-in behavior, modifiers and event consumption. Existing render-revision tests pass with overlays excluded from ray snapshots.

All four actual Release Demo scenes completed 40-frame launches with normal frame-limit shutdown and empty stderr. Scene 2 now uses independent dynamic roots and BodyGravity/VelocityVerlet, replacing scripted hierarchy motion. Scene 3 supplies Earth/Moon selection and a falling ball. The existing destruction wall and glass projectile remain available as Scene 4. These launches establish successful execution, not a realtime-performance guarantee.

Artifacts are ignored under .temp/fields-debug-tests.xml/.log, .temp/fields-release-tests.xml/.log, .temp/fields-*-build.log, .temp/fields-*-configure.log and .temp/fields-demo-1 through fields-demo-4 .log/.err. Run the validated orbit with `./build/fields-release/bin/JadeDemo.exe --scene 2`; F3 controls axes/AABBs and F4 shows physics/forces. No commit was created.

## Scene2 resized to 10x10 (2026-10-10)

Scene2 now contains 100 dynamic cubes in a ten-column, ten-row wall. The camera, 20x20 floor and emissive panel were resized to frame it. The projectile has radius one, mass 20 and initial/interactive launch speed 30; K spawns it 2.5 units in front of the camera. Lazy scene creation, automatic impact, P pause/resume, R reset and 1/2/3 selection remain available.

Debug and Release Demo targets rebuilt without compiler warnings. The actual executables completed Scene2 launches of 24 Debug frames and 48 Release frames. A hidden harness using the actual factory verified exactly 100 cubes sharing a mesh plus one ball, finite positions, sphere contact and 42 cubes displaced more than 0.05 units off the wall plane after 48 fixed steps. This three-worker Release run recorded 0.355 ms creation and 2.237 ms per physics step. A separate harness injected real GLFW callbacks into the current Demo entry point and passed P/K/R/1/2/3/F4/F checks, including exact projectile reset and preservation of Scene3. Existing engine tests were unchanged; the prior 216/216 full runs remain recorded below. Launches under concurrent validation load still logged frame hitches; these checks do not establish a realtime guarantee.

Artifacts: build/validation/scene2-10x10-validation.cpp/.log, scene2-10x10-demo-input.cpp/.log and scene2-10x10-demo-debug.log/scene2-10x10-demo-release.log. The 100x100 measurements below describe the superseded stress fixture.

## Categorized Job System and Scene2 destruction (2026-10-10)

Windows Debug and Release rebuilt the engine, Demo and tests without compiler warnings. The final full runs each passed 216/216 GoogleTest tests across 48 suites (50.012 seconds Debug, 21.512 seconds Release). All 55 public headers compiled independently with MSVC C++20 /W4 /WX /permissive-. An external CMake consumer built with Demo/tests disabled and linked only Jade::Jade. Git diff --check passed. No commit was created.

Scheduler coverage includes exact-once parallel ranges and concurrent producers, reusable DAGs and dynamic counts, prerequisite priority inheritance, weighted dispatch and background category progress, forced stealing from a blocked owner's deque, one-worker nested waits, scoped external assistance, worker exceptions and skipped successors, cycle/identity/ancestor rejection, and concurrent draining shutdown. Physics checks compare sweep-and-prune against independent all-pairs queries, serial/one-worker/three-worker state and contact impulses over 600 steps, transactional rollback after worker failure, recycled identities, and concurrent deterministic physics/path tracing on one pool. Rendering checks cancel or fail actual in-flight tiles and verify that no partial film is committed. The persistent-contact regressions and 20-second pyramid remain included.

The Release public consumer recorded these fixture-specific average step times, with three workers and one-worker controls. Background runs render a separate emissive-sphere scene at 96x64 and are stopped only after physics completes:

| Workers | Fixture | Physics only (ms/step) | With RayTracing (ms/step) | Completed background sample rounds |
| --- | --- | --- | --- | --- |
| 1 | Nine boxes, one island | 0.2708 | 0.2815 | 15 |
| 1 | Sixty boxes, twenty islands | 1.2284 | 1.2837 | 44 |
| 3 | Nine boxes, one island | 0.3564 | 0.3278 | 49 |
| 3 | Sixty boxes, twenty islands | 0.9903 | 0.8588 | 103 |

The separate sparse broad-phase fixture recorded 0.042 ms for 1,000 colliders and 0.431 ms for 10,000 in the earlier Release full run. These measurements have no performance assertions and do not establish general speedups. A connected solver island remains serial; categories and priority do not provide memory exclusion or preemption.

The initial Scene2 version created a vertical wall of exactly 100 columns by 100 rows, with 10,000 dynamic unit cubes sharing one mesh, a static floor, an emissive panel and one radius-three, mass-350 sphere. Small horizontal gaps separate initially unconnected columns. The camera includes the whole wall with a 500-unit far plane. Scene2 is created lazily; the sphere starts moving automatically, K fires it again from the camera, P pauses/resumes, and R rebuilds the selected physics scene. Scene1 and the nine-cube Scene3 remain selectable; --scene 2 permits direct startup.

A hidden harness built from the actual Demo source verified the cube count/shared geometry and advanced 48 fixed steps at 1/120 second: the sphere made contact, 161 cubes moved more than 0.05 units off the wall plane, and all positions remained finite. With three workers, creation took 29.43 ms and physics averaged 152.87 ms/step on this four-logical-processor machine. The actual Demo completed a Release eight-frame Scene2 launch and a Debug two-frame launch. Both exercised frame hitches and substep-debt dropping; this is a stress scene and does not meet the 24-FPS target here. A second harness injects input into the actual Demo entry point through real GLFW callbacks, checking Scene2 P/K/R, lazy 1/2/3 selection and retained scene state, Scene3 reset, F4 and independent render switches. Scene switches are checked after their frame-boundary commit.

Artifacts are ignored under build/validation/jobs-final-debug.xml/.log, jobs-final-release.xml/.log, jobs-headers/, jobs-consumer-source/, jobs-consumer-build/, jobs-consumer-benchmark.log, scene2-validation.cpp/.log, scene2-demo-input.cpp/.log and scene2-demo-debug.log/scene2-demo-release.log. Debug and Release builds are build/windows-debug/ and build/windows-release/.

## Persistent contacts and F4 position regression (2026-10-10)

Debug and Release each passed 202/202 GoogleTest tests across 46 suites. Debug used the normal build/windows-debug/ configuration; Release used build/collision-release/. The normal Debug and Release Demo targets were rebuilt. The affected public Physics/Runtime headers passed standalone MSVC C++20 /W4 /WX compilation, and the hidden harness built from the actual Demo source passed F4/P/K/1/2/3/R/F/I callback checks. No commit was created.

The reported middle-row marker was reproduced as an unstable intersection of nearly parallel box faces. Moving one box by 1 micrometer moved that candidate about 3 millimeters along its edge. The previous check that both witnesses lay on box surfaces did not establish temporal stability or correspondence to solver anchors. The regression now verifies retained corner anchors under small rotations, exact debug/solver point and impulse agreement, surface membership, Scene-edit/deletion invalidation and preservation across pause/resume. Recorded poses and five micron-offset variants also verify stable cold initialization after resetting the cache. The existing 20-second pyramid additionally checks that same-row contact anchors remain at vertical-edge endpoints. Release measurements over the last five seconds were 0.000090 m/s linear speed, 0.000088 rad/s angular speed, 0.003230 m penetration and 0.000098 m drift, with gravity enabled and no sleeping.

A hidden-window OpenGL comparison rendered the same captured Scene with freshly clipped candidates and with persistent solver anchors. The unstable middle-edge marker region contained 24 red pixels before and zero after, with GL_NO_ERROR. The surrounding collider geometry was unchanged. The diagnostic camera approximates the reported view; these captures are not edits of the supplied screenshot. Artifacts: .temp/contact-stable-debug-tests.xml/.log, .temp/contact-stable-release-tests.xml/.log, .temp/contact-reclipped.bmp and .temp/contact-stable.bmp.

## Independent render controls and Archetype Demo (2026-10-10)

Debug and Release each passed 199/199 GoogleTest tests across 46 suites. Engine, Demo and tests rebuilt in both configurations without compiler warnings. Seven affected public headers passed standalone MSVC C++20 /W4 /WX compilation. Renderer::Mode/setMode are removed; render and wireframe flags are independent, with F/I available even when debug shortcuts are disabled.

Real GLFW callbacks verify all four switch combinations, exact modifiers, repeats, global/scene consumption and title/progress labels. Actual OpenGL front-buffer readback verifies a RayTracing image and mesh edges together. A completed one-sample revision and integrator call count remain unchanged across Wireframe toggles. Disabling image rendering starts no integrator, retains the Wireframe flag and resumes fresh samples when enabled. With both scene-rendering flags off, physics still advances, axes/collision overlays remain available and unused Geometry/Material data is not consumed. Typed creation checks constructor argument forwarding, move-only arguments and rollback after configuration failures.

Demo now creates Model/Cube/Ball/StackBox through Archetype constructors, shares one unit-cube mesh and preserves rendering switches across scene changes. Both actual executables completed 40-frame launches with empty stderr and normal shutdown. An ignored harness built from the rewritten Demo source with a hidden window checks F4, P, K, 1/2/3, R, F and I through actual callbacks, including all four rendering combinations. The Debug launch exceeded its frame budget and exercised debt dropping; no realtime-performance guarantee follows from these checks.

Artifacts are ignored under .temp/render-controls-debug-tests.xml/.log, .temp/render-controls-release-tests.xml/.log, .temp/render-controls-*-targeted.xml/.log, .temp/render-controls-header-checks.log, .temp/render-controls-*-demo.log/.err and .temp/render-controls-demo-input.log. Builds remain build/collision-debug/ and build/collision-release/. No commit was created.

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
