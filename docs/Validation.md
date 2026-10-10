# Windows x64 validation

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
