# Windows x64 validation

Validated on 2026-10-10 with MSVC 19.51, C++20, CMake/Ninja and Mesa 26.2.4 llvmpipe for window/display checks. Path-tracing computation runs on CPU workers and needs no graphics context.

| Check | Result |
| --- | --- |
| Windows Debug engine, Demo and tests build | Passed without compiler warnings |
| Windows Release engine, Demo and tests build | Passed without compiler warnings |
| Debug GoogleTest | 155/155 passed, 37 suites |
| Release GoogleTest | 155/155 passed, 37 suites |
| Standalone public headers | All 41 passed MSVC C++20 /W4 /WX |
| External CMake consumer | Built with Demo/tests disabled, linking only Jade::Jade |
| Headless consumer render | 640x424, 256 SPP, 5 instances, 12 emissive triangles |
| Debug and Release Demo launches | 240 frames each, 960x640 windows with 480x320 progressive samples, exit 0 and empty stderr |
| Source/header inventory and first includes | All maintained C++ files explicitly listed; corresponding header included first |
| Git whitespace check | Passed |

## Physical rendering

RendererTests replaces the obsolete lit OpenGL tests with headless geometric and transport checks. Nearest hits, clipping, back faces, shared resources and entity identities are checked directly. Instance transforms under nonuniform scale and inherited shear agree with baked world geometry. SAH BVH traversal agrees with an independent brute-force triangle oracle. Detached snapshots retain meshes beyond Assets lifetime and ignore later component edits. Degenerate triangles, missing materials, foreign handles and invalid/projective transforms have defined outcomes. Cancelled compilation can be retried.

SahTests verifies independent Material preset values and generic component composition, synchronous MeshData compilation before asset publication, immutable shared BLAS ownership, OBJ cache reuse and original triangle identities after excluding degenerate triangles. TLAS checks cover camera/material reuse, transform refit, quality-triggered SAH rebuilding, layout/mesh changes and published snapshot isolation. Emitter distribution checks distinguish ordinary instance changes from emission, sidedness and emitting-transform changes. Cancelled or failed scene compilation preserves the last successful compiler state. Private BVH tests cover near-first pruning, shadow early exit, coincident centroids, the 48-level depth cap, extreme finite areas and transactional cancellation.

An independent median-split reference is used only in tests. In the nonuniform fixture (48 small clustered bounds and one distant larger bound), estimated SAH cost was 1.015094 versus 7.972450 and candidate primitive tests were 1 versus 4 for the selected ray. The measured Release SAH build took 30.1 microseconds in this run; Debug took 405.5 microseconds while other builds ran. Timing is recorded without a pass/fail threshold. These fixture measurements do not establish a general rendering-speed improvement.

Camera tests cover perspective/orthographic ray generation, aspect ratio and near/far planes. Secondary transport beyond the primary far plane remains visible. Diffuse sampling recovers reflectance; smooth/rough metal and dielectric sampling agree with their PDFs. Glass tests cover refraction, total internal reflection and the IOR=1 limit. No-emitter scenes return black; one-sided emission and HDR values are preserved. Independent checks cover emitter tessellation invariance, shadows and indirect red-wall color bleeding.

Film checks cover HDR running means, invalid radiance rejection, exposure/tone mapping and sRGB encoding. Identical seeds/sample counts produce identical images with one and three workers. Cancellation commits no partial sample round. Custom integrators work headlessly and cannot be replaced while an Application session is active.

## Scheduling and debug presentation

Deterministic controlled-backend tests verify coalescing of camera revisions, cancellation during resize/mode changes, rejection of stale images and worker-exception propagation. Application F checks verify that wireframe bypasses the computational backend and returning to physical mode resumes with fresh accumulation. Missing backend registration and replacement during Run are rejected.

Real OpenGL readback verifies sRGB image orientation and bytes, unlit triangle edges and unit RGB axes. Existing Scene, hierarchy, Assets, mesh/OBJ, math, CameraControl, module rollback, event dispatch, input ordering, timing and logger suites remain maintained against the new APIs. Input integration invokes GLFW callbacks on hidden windows; automated tests do not establish manual keyboard/mouse behavior or realtime rendering performance.

## Demo launches

Both executables were launched from their actual bin directories using --frames 240. Logs confirm deployed bunny loading, llvmpipe presentation, physical 480x320 sample completion, continued accumulation and normal frame-limit shutdown. Demo uses resolutionScale 0.5 for its 960x640 window; the engine default remains 1. Presentation/input pacing and physical sample throughput are separate; these measurements are local observations, not a performance guarantee. The IDE debug button and manual controls were not exercised.

## Public consumer and visual output

The standalone-header check exposes only Jade/include. The external CMake consumer additionally rejects leaked GLFW/GLAD include paths at compile time. It captures a SceneSnapshot, compiles RenderScene, accumulates Film through RayTracing and exports RGBA8 output without constructing Application or opening a window.

The exported preview was visually inspected. It shows a grounded diffuse bunny, metal cube, closed glass sphere and emissive ceiling mesh. Floor illumination, contact shadows, metal reflections and glass refraction come from the integrator. Low-sample noise remains expected; this version has no denoiser, textures, volumes, nested media or dedicated caustic algorithm.

Validation artifacts remain ignored: .temp/sah-debug-tests.xml/.log, .temp/sah-release-tests.xml/.log, .temp/sah-header-checks.log, .temp/ray-consumer/, .temp/sah-consumer.log and .temp/sah-preview.bmp. Demo stdout/stderr are .temp/sah-windows-debug-demo.log/.err and .temp/sah-windows-release-demo.log/.err. The independent consumer build is build/ray-consumer/. These results describe the current ray-based renderer and supersede historical raster-lighting validation.
