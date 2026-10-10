# Windows x64 validation

Validated on 2026-10-10 with MSVC 19.51, C++20, CMake/Ninja and Mesa 26.2.4 llvmpipe for window/display checks. Path-tracing computation runs on CPU workers and needs no graphics context.

| Check | Result |
| --- | --- |
| Windows Debug engine, Demo and tests build | Passed without compiler warnings |
| Windows Release engine, Demo and tests build | Passed without compiler warnings |
| Debug GoogleTest | 172/172 passed, 39 suites |
| Release GoogleTest | 172/172 passed, 39 suites |
| Python-free clean Debug build | Passed with Python/Python3 package discovery disabled; no virtual environment created |
| Standalone public headers | All 45 passed MSVC C++20 /W4 /WX |
| External CMake consumer | Built with Demo/tests disabled, linking only Jade::Jade |
| Headless consumer render | 640x424, 256 SPP, 5 instances, 12 emissive triangles |
| Debug and Release Demo launches | 240 frames each, default 960x640 Rasterization preview, normal frame-limit shutdown and empty stderr |
| Actual backend/input integration | 240 frames with repeated I switching, F pause/resume and a large reversible wheel gesture |
| Source/header inventory and first includes | All maintained C++ files explicitly listed; corresponding header included first |
| Git whitespace check | Passed |

## Physical rendering

RendererTests replaces the obsolete lit OpenGL tests with headless geometric and transport checks. Nearest hits, clipping, back faces, shared resources and entity identities are checked directly. Instance transforms under nonuniform scale and inherited shear agree with baked world geometry. SAH BVH traversal agrees with an independent brute-force triangle oracle. Detached snapshots retain meshes beyond Assets lifetime and ignore later component edits. Degenerate triangles, missing materials, foreign handles and invalid/projective transforms have defined outcomes. Cancelled compilation can be retried.

SahTests verifies independent Material preset values and generic component composition, synchronous MeshData compilation before asset publication, immutable shared BLAS ownership, OBJ cache reuse and original triangle identities after excluding degenerate triangles. TLAS checks cover camera/material reuse, transform refit, quality-triggered SAH rebuilding, layout/mesh changes and published snapshot isolation. Emitter distribution checks distinguish ordinary instance changes from emission, sidedness and emitting-transform changes. Cancelled or failed scene compilation preserves the last successful compiler state. Private BVH tests cover near-first pruning, shadow early exit, coincident centroids, the 48-level depth cap, extreme finite areas and transactional cancellation.

An independent median-split reference is used only in tests. In the nonuniform fixture (48 small clustered bounds and one distant larger bound), estimated SAH cost was 1.015094 versus 7.972450 and candidate primitive tests were 1 versus 4 for the selected ray. The measured Release SAH build took 30.1 microseconds in this run; Debug took 405.5 microseconds while other builds ran. Timing is recorded without a pass/fail threshold. These fixture measurements do not establish a general rendering-speed improvement.

Camera tests cover perspective/orthographic ray generation, aspect ratio and near/far planes. Secondary transport beyond the primary far plane remains visible. Diffuse sampling recovers reflectance; smooth/rough metal and dielectric sampling agree with their PDFs. Glass tests cover refraction, total internal reflection and the IOR=1 limit. No-emitter scenes return black; one-sided emission and HDR values are preserved. Independent checks cover emitter tessellation invariance, shadows and indirect red-wall color bleeding.

Film checks cover HDR running means, invalid radiance rejection, exposure/tone mapping and sRGB encoding. Identical seeds/sample counts produce identical images with one and three workers. Cancellation commits no partial sample round. Custom integrators work headlessly and cannot be replaced while an Application session is active.

## Scheduling and debug presentation

Deterministic controlled-backend tests verify coalescing of camera revisions, cancellation during resize/mode changes, rejection of stale images and worker-exception propagation. Registry tests cover same-type replacement, registration order, last deferred selection winning and locking every registered backend during Run. I works with debug disabled and respects event consumption, modifiers and repeats. Application F checks verify that wireframe bypasses both backends; I changes selection while paused and leaving wireframe resumes the selected backend with fresh work. A 240-frame integration uses real Rasterization and RayTracing backends and GLFW input callbacks. Missing backend registration and replacement during Run are rejected.

The wheel regression drives perspective and orthographic lenses beyond their interactive bounds and reverses the same gesture, restoring the initial lens and pose. GPU readback after perspective FOV changes returns to the original pixels without rebuilding unchanged shadow/probe caches. The original angular FOV scaling could reach 179 degrees and discard scroll at the clamp; the fix scales tan(FOV/2), limits interactive perspective zoom to 120 degrees and retains the accumulated gesture.

Real OpenGL readback verifies sRGB image orientation and bytes, unlit triangle edges and unit RGB axes. Existing Scene, hierarchy, Assets, mesh/OBJ, math, CameraControl, module rollback, event dispatch, input ordering, timing and logger suites remain maintained against the new APIs. Input integration invokes GLFW callbacks on hidden windows; automated tests do not establish manual keyboard/mouse behavior or realtime rendering performance.

## Demo launches

Both executables were launched from their actual bin directories using --frames 240. Logs confirm deployed bunny loading, llvmpipe presentation, Rasterization selection and normal frame-limit shutdown. Demo starts with the full 960x640 raster preview; I selects RayTracing with resolutionScale 0.5 (480x320). The engine default remains 1. Presentation/input pacing and physical sample throughput are separate; these measurements are local observations, not a performance guarantee. The IDE debug button and manual controls were not exercised.

## Public consumer and visual output

The standalone-header check exposes only Jade/include. The external CMake consumer additionally rejects leaked GLFW/GLAD include paths at compile time. It captures a SceneSnapshot, compiles RayScene, accumulates Film through RayTracing and exports RGBA8 output without constructing Application or opening a window.

Paired Rasterization and RayTracing previews were exported and visually inspected with the same camera and 640x424 resolution; RayTracing used 256 SPP. Both show the diffuse bunny, metal cube, glass sphere and emissive ceiling mesh. On this llvmpipe run, raster initialization took 2027.2 ms, a cached raster frame took 15.1889 ms and the 256-SPP CPU render took 17390.1 ms. These observations have no pass/fail threshold and do not predict hardware GPU performance.

RasterizationTests performs actual OpenGL HDR readback. Tests cover no-emitter black output, emission sidedness, shared display encoding, area-PDF normalization against independent numerical integration, off-camera occluders, colored thin glass shadows, reflection probe invalidation/budgets, SSR with perspective and orthographic cameras, IOR=1 transmission, back-face refraction, TIR and missing vertex normals. Raster lighting is an approximation: contact shadows and glass can show map/sample/screen-coverage artifacts, and it has no diffuse GI, caustics or nested media. Path tracing remains the physical reference; low-sample noise remains expected, and there is no denoiser, textures, volumes or dedicated caustic algorithm.

Validation artifacts remain ignored: .temp/no-python-debug-tests.xml/.log, .temp/renderer-release-tests.xml/.log, .temp/renderer-header-checks.log, .temp/ray-consumer/, .temp/renderer-consumer.log and .temp/renderer-preview-{raster,ray}.bmp. Demo stdout/stderr are .temp/renderer-debug-demo.log/.err and .temp/renderer-release-demo.log/.err. Independent builds are build/no-python-debug/, build/renderer-release/ and build/ray-consumer/. GLAD is compiled from checked-in C source and shaders are embedded with CMake; these checks invoke no Python tooling.
