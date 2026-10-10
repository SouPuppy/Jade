# Windows x64 Validation

## Current test entry point

Sixty-three GoogleTests live under root-level `tests/` and run directly through `JadeSmokeTests.exe`, without CTest. SmokeTests.cpp covers geometry/timing/application checks; MathTests.cpp checks vectors, matrices and inverse-transpose normals; GeometryTests.cpp covers Camera, Triangle, Object and ownership. MeshTests.cpp checks cube topology, mesh snapshots, OBJ loading/errors and the bundled bunny. RendererTests.cpp checks indexed meshes, lighting, pixels, axes, depth and wireframe. InputTests.cpp covers keyboard/mouse adapters, F3 toggling, subscription dispatch, loop stopping/recovery and Motion controls.

Demo creates Camera and generic Objects, assigns Mesh::cube(2) and Mesh::load("assets/bunny.obj"), and initially shows filled faces and unit world axes. WASD/arrows, Space/Ctrl+Space, Shift/Ctrl+W, left-button drag, wheel zoom, Q/E roll, O projection toggle and F wireframe toggle use the event API; F3 uses the default Application action. Timed events use dt. Headers remain split .hpp files, builtin cube generation lives in Mesh/builtins, and OBJ parsing, backends, input state, dispatch, matrices and clock live under src/internal/.

## Mesh and Axis Gizmo validation

Validated on 2026-10-10 with MSVC 19.51, CMake/Ninja, C++20 and Mesa llvmpipe:

| Configuration | Engine and Demo build | GoogleTest | Direct Demo launch from bin |
| --- | --- | --- | --- |
| windows-debug | Passed without warnings | 63/63 passed | Assets loaded, frame logs, close exit 0 |
| windows-release | Passed without warnings | 63/63 passed | Assets loaded, frame logs, close exit 0 |

Mesh tests check side length, face winding, independent hard-face normals, valid indices,
owned snapshots and unchanged Object state after invalid replacements. OBJ checks cover triangles,
convex quads, positive/negative indices, all supported face reference forms, explicit normals,
generated area-weighted normals, normal seams, comments, absent files and malformed records with
path/line diagnostics. The bundled bunny loads as 453 vertices and 948 triangles with finite normals.

Pixel tests verify the renderer follows indices rather than vertex-array order, changes geometry
after setMesh, skips empty meshes, uses and rotates normals for lighting, and renders the loaded
bunny in both projections and wireframe. Matrix checks verify nonuniform scale keeps transformed
normals perpendicular to tangents. Existing resize, depth, clipping, overlay and wireframe checks
continue to pass using Object meshes instead of the removed Cube type.

Axis pixel checks cover all three colors, visible unit segments and absence beyond their endpoints,
both projections, landscape/portrait resize, changed camera rotation, visibility over cube meshes,
and depth/polygon state preservation. F3 integration checks cover default/configured state,
press/repeat handling, release/repress, consumption, exact modifiers and public setter/query.
The rotated test camera keeps all sampled axis positions inside the framebuffer.

All 21 public headers and Demo passed independent MSVC C++20 syntax checks with /W4 /WX using
only the public include directory. All 22 implementation/header pairs include the corresponding
header first and are explicitly listed in CMake. XML reports are
build/validation/windows-debug-lines.xml and windows-release-lines.xml. Actual OpenGL framebuffer
previews mesh-perspective.png, mesh-orthographic.png and mesh-wireframe.png were exported under
ignored build/validation/ and visually inspected. Demo launch checks use the executable's bin
directory, verifying asset deployment and paths. The IDE debug button was not manually exercised.

## Previous Events and Motion validation

Validated on 2026-10-10 with MSVC 19.51, CMake/Ninja, C++20 and Mesa llvmpipe:

| Configuration | Engine and Demo build | GoogleTest | Direct Demo launch |
| --- | --- | --- | --- |
| windows-debug | Passed without warnings | 49/49 passed | Frame logs, close exit 0 |
| windows-release | Passed without warnings | 49/49 passed | Frame logs, close exit 0 |

Tests cover quick key/button press/release pairs between frames, repeat suppression, exact C-/S- matching, focus-loss release/reset, ordered mixed key/button/cursor/scroll input and cursor baseline restoration. Application checks invoke the installed GLFW key/button/focus/cursor/scroll callbacks directly on hidden windows and verify Held timing, frame callbacks, propagation, cancellation, stop/resume and recovery after callback exceptions. Drag checks include a complete press/move/release sequence between frames and modifiers changing before release. Button Held uses the same dt as FrameUpdate. No automated check injects real keyboard or mouse messages; interactive controls were not manually exercised.

Motion checks cover all six translation directions, movement at default distance, camera axes after yaw/pitch, pitch limits, angle wrapping, and unchanged transforms after invalid input. Roll checks cover banking after combined yaw/pitch without changing front, rotated right/up axes and movement after banking. Zoom checks cover magnification in both projections, fractional inverse scrolling, extreme input bounds, preserved pose/mode/clip planes, zero increments and nonfinite input rejection. Camera view and direction getters share yaw * pitch * roll orientation; Cube retains its previous transform order.

All twenty public headers and Demo passed independent MSVC C++20 syntax checks with /W4 /WX using only the public include directory. Misspelled KeyHeld<"Spcae"> and MousePressed<"Lfet"> were separately verified to fail compilation with the intended binding diagnostics. All eighteen source/header pairs and explicit CMake entries were checked. Current XML reports are build/validation/windows-debug-lines.xml and windows-release-lines.xml. [Events](Events.md) records the API and lifetime contract.

The O integration check sends a press and repeat, verifies only one switch, applies scroll to each active mode, and switches back without resetting either zoom parameter, pose or clip planes. Invalid projection modes preserve the current mode. A user-reported wheel problem remains unclassified; the automated scroll checks do not establish that the reported behavior is resolved.

## Previous Geometry, Objects and wireframe validation

Validated on 2026-10-10 with MSVC 19.51 and Mesa llvmpipe:

| Configuration | Engine and Demo build | GoogleTest |
| --- | --- | --- |
| windows-debug | Passed without warnings | 25/25 passed |
| windows-release | Passed without warnings | 25/25 passed |

Public headers and implementations now separate Geometry (screen-space Line/Triangle) from Objects (Object/Cube/Camera). Cube and Camera inherit Object; the Camera shares its position storage and preserves position validation through Object references. Tests verify local camera rotation/scale changes the view and lookAt resets its rotation offset.

Pixel checks verify wireframe Cube bounds in both projection modes, reduced colored pixel coverage, empty Triangle interiors with visible edges, unchanged Line rendering and restored fill after switching off. An Application integration check verifies the public setter reaches both polygon modes across bounded Run calls. All eleven public headers and Demo passed individual MSVC C++17 syntax checks with /W4 /WX using only the public include directory. All fourteen source/header pairs remain intact. XML reports were refreshed under ignored build/validation/; a real OpenGL wireframe preview was exported as cube-wireframe.png.

## Previous Cube and camera validation

Validated on 2026-10-10 using MSVC 19.51, CMake/Ninja and Mesa 26.2.4 llvmpipe, with GALLIUM_DRIVER absent in the launching process:

| Configuration | Engine and Demo build | GoogleTest | Direct Demo launch |
| --- | --- | --- | --- |
| windows-debug | Passed without warnings | 21/21 passed | Frame logs, close exit 0 |
| windows-release | Passed without warnings | 21/21 passed | Frame logs, close exit 0 |

Pixel tests verify filled Triangle interiors/background, landscape and portrait resizing, square centered Cube silhouettes in both projection modes, distance and FOV changes in perspective, distance-independent orthographic size, draw-order-independent depth occlusion, depth clearing between frames, and rejection outside near/far clipping planes. They also exercise zero-sized frames and degenerate 2D primitives.

Math tests verify signed 2D cross products, perpendicular 3D cross products, eye-to-origin view transforms, known near/far NDC values, aspect adjustment, and scale/rotation/translation order. Camera and geometry tests verify failed updates preserve prior values and factory references stay stable across creation and repeated Run calls.

All eleven public headers passed individual MSVC C++17 syntax checks using /W4 /WX and only the public include directory. The Application check instantiates all four supported create types. Demo passed the same independent syntax check. All fourteen implementation/header pairs were verified. The old Vec2D spelling remains a valid alias through Vector.hpp.

GoogleTest XML reports were refreshed under ignored build/validation/ as windows-debug-lines.xml and windows-release-lines.xml. Actual OpenGL framebuffer previews were exported as cube-perspective.png and cube-orthographic.png and visually inspected. The real Demo ran for over two seconds in each configuration and closed with exit 0; no environment variable was added to the parent process. The IDE debug button was not manually exercised.

## Previous public header separation validation

At the header-separation stage, public declarations lived in individual Application.hpp, Log.hpp, Vector.hpp, Line.hpp, Color.hpp, Window.hpp and Renderer.hpp headers directly under `Jade/include/Jade/`. Vector.hpp declares the unchanged Vec2D type. `Jade.hpp` only includes these headers as an optional convenience entry point. Configuration and related result types remain with their owning interface; there are no Math or Geometry aggregation headers. Engine implementation `.cpp` files retain their matching private `.hpp` files under `src/`; CMake explicitly lists these pairs. Internal files use specific public headers instead of the entry point.

On 2026-10-10, all eight public headers passed independent MSVC C++17 syntax checks with `/W4 /WX` and only the public engine include directory. The checks exercise declarations and Application's `create<Line>()` template through both an individual header and Jade.hpp. All ten implementation/header pairs were verified. Debug and Release each rebuilt the engine and GoogleTest executable and passed all eight tests using individual public headers. Reports are under ignored `build/validation/` as `windows-debug-headers.xml` and `windows-release-headers.xml`.

At that stage Demo contained an unsupported `app.createn<Jade::Camera>();` draft, so only the engine/test target and supported standalone consumers were validated. The Cube/camera implementation above replaces the draft and restores full Demo builds.

## Previous single-segment API validation

Line now stores exactly two endpoints, renders with `GL_LINES`, and exposes `setPos(start, end)`. `ApplicationConfig::frameLimits` defaults to 24 frames per second, and frame logs use `frame_limits`. On 2026-10-10, Debug and Release both rebuilt Demo and passed all eight GoogleTests with the updated API. The tests check that invalid position updates preserve both endpoints, a moved Line replaces its old pixels on the next draw, degenerate Lines leave the clear untouched, and frame deadlines and logs use the 24 FPS configuration. Debug and Release GoogleTest XML reports were refreshed under `build/validation/`.

## Previous automatic software driver validation

Directly launching Demo with `GALLIUM_DRIVER` unset reproduced exit code `0x80070057` after Mesa logged `D3D12 (Microsoft Basic Render Driver)`. The Line pixel test also failed with that backend. CMake's `VS_DEBUGGER_ENVIRONMENT` property only applies to Visual Studio generators and was ignored by the project's Ninja presets, so validation-shell settings did not cover normal launches. See the [CMake property documentation](https://cmake.org/cmake/help/latest/prop_tgt/VS_DEBUGGER_ENVIRONMENT.html).

Software rendering builds now select llvmpipe in the process environment before GLFW initialization, preserving an explicit non-empty driver selection. Builds with software rendering disabled do not change the environment. The debugger-specific driver setting was removed.

Validated on 2026-10-10 with `GALLIUM_DRIVER` absent from the launching process:

| Configuration | GoogleTest | Direct Demo launch |
| --- | --- | --- |
| windows-debug | 8/8 passed | llvmpipe, frame logs at the configured 24 FPS, close exit 0 |
| windows-release | 8/8 passed | llvmpipe, frame logs at the configured 24 FPS, close exit 0 |

Both builds completed without compiler warnings. Each real Demo process ran for over two seconds and exited after a close message to its own GLFW window. No driver variable was added to the parent process. This launch check is a local validation artifact under ignored `build/validation/`, outside the maintained test suite; the IDE debug button was not manually exercised.

## Previous migration validation

The migration was validated on 2026-10-10 with MSVC 19.51.36260, CMake 4.3.1-msvc1, Ninja 1.13.2 and Python 3.14.8. GLAD used its isolated environment with Jinja2 3.1.6 and MarkupSafe 3.0.3. GLFW 3.4 and GLAD 2.0.8 were fetched from pinned archives; GLAD generated OpenGL 3.3 Core with REPRODUCIBLE and no extensions. Rendering used Mesa 26.2.4 / llvmpipe.

Before replacing the test runner, Debug and Release each passed five CTest entries. Builds with Demo disabled each passed three entries, and the standalone consumer passed its own rendering check. These are historical results for the earlier test setup.

The migration also checked engine-only builds, Mesa deployment and removal, static GLFW / GLAD linkage, matching `/MDd` and `/MD` runtime flags, and absence of generated files from maintained source directories. The consumer included public headers and linked only `Jade::Jade`; compilation rejected leaked GLFW or GLAD header search paths.

Visual Studio's CMake File API exposed the `JadeDemo` target and `bin/JadeDemo.exe` output. Program execution was validated through test processes; the IDE debug button was not manually exercised.

Old builds, dependency installations, configuration and removed validation scripts were archived under `.local/` during the migration. Existing backups were retained. No Git staging, commits, resets or history edits were performed by the migration work.
