# Windows x64 Validation

## Application debug controls and Scene modules

Validated on 2026-10-10 with MSVC/C++20, CMake/Ninja and real OpenGL through llvmpipe. Debug and Release engine/Demo builds passed without compiler warnings; both full GoogleTest suites passed 139/139 tests.

Fourteen new tests cover per-type/per-Scene installation without constructing module instances, repeated use and activation, exception rollback of new nested modules/subscriptions, retention of existing modules/subscribers, recursive installation rejection and retry, and expired connections. Application debug checks cover opt-in F/F3/O in both builds, exact modifiers and ignored repeat, propagation/Stop/app.stop, renderer-backed wireframe state, retained-scene projection during deferred selection, zoom preservation and absent/destroyed/incomplete cameras. CameraControl checks cover all movement bindings and speeds against actual dt, roll/zoom, dynamic camera targeting, duplicate installation, ordered dragging, release/focus/camera/scene resets, empty scenes and missing components, and absence of a Scene ownership cycle. The existing F3 integration test explicitly enables debug and still checks independent initial axis visibility.

All 28 public headers compiled independently using only Jade/include with MSVC C++20 /W4 /WX. A separate CMake consumer with Demo/tests disabled linked only Jade::Jade, included Module/CameraControl.hpp, installed it twice in both scenes, enabled config.debug, queried wireframe state and ran scene switching/rendering/unload successfully.

Automated Debug and Release Demo launches from bin injected F/F3/O, switched 1 -> 2 -> 1, and posted WM_CLOSE. Both verified one wireframe on/off and axes off/on transition, orthographic projection in each scene and restored showroom projection back to perspective, three scene activations, llvmpipe/performance output, empty stderr, plain redirected logs and exit code 0. These are automated launch/input checks; no manual visual inspection is claimed.

Reports/output are build/validation/windows-debug-modules.xml/.log and windows-release-modules.xml/.log. Focused checks are modules-debug-focused.log; independent headers are module-headers.log; external consumer source/build/output are scene-consumer/, scene-consumer-build/ and module-consumer.log. Demo stdout/stderr are windows-debug-modules-demo.log/.err and windows-release-modules-demo.log/.err. All artifacts remain under ignored build/validation/.

## Sphere mesh builder

Validated on 2026-10-10 with MSVC/C++20, CMake/Ninja and real OpenGL through llvmpipe. Debug and Release engine/Demo builds passed without compiler warnings; both full GoogleTest suites passed 125/125 tests.

Mesh::sphere(radius=0.5, segments=32, rings=16) generates centered indexed triangles with smooth radial normals, shared poles and wrapped longitude indices. Tests check requested radius, unit normals, default counts, minimum/odd/default tessellation, nondegenerate outward winding, two oppositely directed triangle uses per edge, Euler closure, Assets registration and invalid parameters/count overflow before allocation. A real OpenGL readback checks circular silhouette, clear corners, lighting variation and wireframe rendering.

Demo's hierarchy now shares a sphere mesh across sun/earth/moon. Automated Debug and Release launches from bin switched 1 -> 2 -> 1 and closed successfully, with empty stderr and plain redirected logs. Reports/output are build/validation/windows-debug-sphere.xml/.log and windows-release-sphere.xml/.log; Demo output is windows-debug-sphere-demo.log/.err and windows-release-sphere-demo.log/.err. These checks do not claim manual visual inspection.

## Scene, hierarchy and lifecycle refactor

Validated on 2026-10-10 with MSVC 19.51, CMake/Ninja, C++20 and real OpenGL through llvmpipe:

| Configuration | Engine and Demo build | GoogleTest | Demo scene switching |
| --- | --- | --- | --- |
| windows-debug | Passed | 121/121 passed | 1 → 2 → 1, close exit 0 |
| windows-release | Passed | 121/121 passed | 1 → 2 → 1, close exit 0 |

The maintained API is Scene/Entity with iterable component views, explicit scene/camera selection, scene-owned controls and Application-owned shared Assets/global events. World, its headers and Application camera/world methods have been removed; there are no compatibility aliases. Material remains deferred and MeshRenderer still uses a MeshHandle and Color.

Twenty-one new tests cover independently empty scenes and weak view lifetimes; hierarchy ownership/cycles, attachment ordering, local-pose-preserving reparenting, identity groups and component removal; complete subtree invalidation before reentrant component destruction; 4,096-level nonrecursive traversal; full matrices, inherited shear and normal transforms; parented camera eye/rotation with scale-independent projection; deferred selection, last-request wins, unload, lifecycle ordering/consumption/reentry, exception recovery and pending-request cancellation; single-Application activation reservations, expired connections; global-before-scene input, preserved physical Held state and scene-consumed default actions. Existing tests were migrated to Scene without reinstating implicit cameras.

Three of those tests inspect real OpenGL pixels for inherited position/scale, removing parent Transform, cascading deletion, inverse-transpose lighting under nonuniform parent scale and child rotation, and parented-camera projection equivalence. Existing depth, resize, wireframe, projection, axes, mesh, asset, input, scheduler and logger checks remain in the full suite.

All 27 public headers compiled independently using only Jade/include with MSVC C++20 /W4 /WX. A separate CMake consumer disabled both Demo and tests, linked only Jade::Jade, created explicit cameras and parented mesh entities, iterated views, queried Matrix4, switched scenes, verified lifecycle/dt and ran clear-only after unload. It built and exited successfully.

Debug and Release Demo checks launched from bin with no GALLIUM_DRIVER override, hid the GLFW window, injected the two scene-selection key transitions and posted WM_CLOSE. Both logged showroom → hierarchy → showroom activation and performance summaries, produced empty stderr, contained no ANSI in redirected logs and exited with code 0. These are automated launch/input checks; no manual visual inspection is claimed.

Reports and stdout are build/validation/windows-debug-scenes.xml/.log and windows-release-scenes.xml/.log. Standalone header output is scene-headers.log; the external consumer source/build/output is scene-consumer/, scene-consumer-build/ and scene-consumer.log. Demo stdout/stderr is windows-debug-scenes-demo.log/.err and windows-release-scenes-demo.log/.err. The added reservation test also has focused Debug/Release results in scene-reservation-debug.log and scene-reservation-release.log. All artifacts stay under ignored build/validation/.

## Previous logger refactor

Validated on 2026-10-10 with MSVC 19.51, CMake/Ninja and C++20:

| Configuration | Engine and Demo build | GoogleTest |
| --- | --- | --- |
| windows-debug | Passed without compiler warnings | 100/100 passed |
| windows-release | Passed without compiler warnings | 100/100 passed |

Twelve LogTests verify default stdout/stderr routing, raw braces and standard
format specifiers, filtering before formatter invocation, Off, owning Logger names/records,
append/flush behavior and ordered file replacement, failed file-open rollback,
callback logging/Flush/Configure reentry, throwing callbacks/formatters and 400
complete unique messages from four concurrent writers. The exception test
intentionally emits four fallback error diagnostics. Default configuration exposes
Debug and higher levels in both builds, with Trace suppressed.
All maintained callers use named Logger objects with `logger << info/warn/error(...)`.
The previous public Write API and uppercase Info/Error helpers are removed without
compatibility wrappers. Entry tests verify that creation/destruction never emits,
move construction/assignment transfers an owning payload, moved-from/consumed
entries cannot emit twice, const Logger insertion supports chaining, and threshold
changes do not revive filtered entries. Existing Logger objects observe output
reconfiguration, and four threads write through the same named Logger.

The format test fixes a local timestamp at `15:27:07.591522`, verifies six-digit
microseconds including leading zeros, right-aligned levels and ANSI reset boundaries.
Console-capture and file tests use the new
`HH:MM:SS.ffffff LEVEL [Logger] - message` format and reject ANSI in plain outputs.
A separately compiled LoggerTerminalProbe verified native Windows terminal
handles and colors for all six levels through a PTY; redirected streams and the
simultaneously written log file contained no ANSI escapes. The session initially
sets NO_COLOR=1 and TERM=dumb, which correctly suppress colors; enabling terminal
colors was tested by changing those variables only in the probe's launching shell.

Four deterministic FrameDiagnostics tests check one-second summaries, separate
frame intervals and work time, fresh windows, startup warm-up, scheduling jitter,
hitch thresholds and cooldown, minimized-window reset and coincident completion
times. Application integration checks paced repeated Runs, summary fields and
frequency, explicit per-frame Trace, disabled diagnostics, lifecycle records,
exception propagation/recovery, requested stop and preservation of an explicitly
configured Info threshold. Asset logging checks one successful cache publication,
quiet cache hits, load failures and preservation of the original exception/cache.
The old logFrames option is removed; logPerformance enables these diagnostics.

Five EventLogTests verify Debug binding/subscription/edge records, exact matching,
handler counts and consuming subscription IDs; Trace-only high-frequency events;
Info/Off filtering without suppressing callbacks or exception propagation;
standard/non-standard callback errors and recovery; cancellation/registration
snapshot semantics and once-only pruning records; and sink reentry during both
registration and pruning. Existing InputTests still cover actual GLFW adapters,
propagation, input queue order, Held timing and Application stop/resume behavior.

Full-suite reports are `build/validation/windows-debug-logger-refactor.xml` and
`build/validation/windows-release-logger-refactor.xml`. Debug/Release stdout/stderr
is saved to `build/validation/windows-debug-logger-refactor.log` and
`build/validation/windows-release-logger-refactor.log`. Demo was rebuilt
in both configurations; manual Demo interaction was not repeated for this change.
The following Entity/Assets section records the preceding 75-test baseline.

## Previous Entity/Assets implementation

Validated on 2026-10-10 with MSVC 19.51, CMake/Ninja, C++20 and Mesa 26.2.4 llvmpipe:

| Configuration | Engine and Demo | GoogleTest | Demo launched from bin |
| --- | --- | --- | --- |
| windows-debug | Passed without compiler warnings | 75/75 passed | Models loaded, frame logs, close exit 0 |
| windows-release | Passed without compiler warnings | 75/75 passed | Models loaded, frame logs, close exit 0 |

The maintained entry point is JadeSmokeTests.exe. CoreTests checks empty identities, custom/noncopyable components, duplicate/missing operations, generation reuse, foreign/expired owners, destructor reentry, construction failures, stable component references and query mutation snapshots. Asset checks cover immutable snapshots, shared resources with independent entity pose/color, lifetime, canonical path caching, failed-load retry and unchanged successful cache entries after file edits.

ComponentTests checks aggregate data, validation at use and explicit camera selection, including foreign/incomplete/dead cameras, removed components, clear-only operation and recovery. Math/Input tests cover quaternion composition/normalization/inverse, view/direction agreement, look-at cases around 180 degrees, camera scale ignored by view, full pitch flips, local yaw/pitch after roll, six-axis movement, repeated control normalization, lens zoom and O toggling without resetting settings.

MeshTests retains cube topology/normals and OBJ syntax/error coverage, adds Vec3 line/triangle builders and topology validation, and loads the bundled 453-vertex/948-triangle bunny. RendererTests exercises the real World -> RenderSystem -> OpenGL path: shared meshes with independent colors/transforms, index replacement by asset handle, normal lighting, bunny/cube projections and wireframe, landscape/portrait resize, draw-order depth, clipping, lines in front/behind triangles, unit axes and depth/polygon restoration. Invalid or missing active cameras skip world/gizmo drawing and valid replacement resumes it. Resize checks present between simulated frames, matching the application loop.

Scheduling and event assertions remain: absolute deadlines, frame logs/counters, paced repeated Runs, ordered key/button/cursor/scroll queues, quick taps, repeat suppression, focus release, exact modifiers, Held/FrameUpdate dt, propagation, cancellation, stop/resume and exception recovery. Input integration invokes installed GLFW adapters on hidden windows; real mouse/keyboard messages were not injected. The user-reported wheel issue remains unclassified beyond these tested scroll/lens behaviors.

All 25 public headers and Demo passed independent MSVC C++20 syntax checks with /W4 /WX using only the public include directory. An independent CMake consumer linking only Jade::Jade also built and ran three clear-validated frames with custom components and shared meshes. Its compile-time check confirmed GLFW/GLAD include paths are not exported.

All 23 implementation/header pairs include the corresponding header first and are explicitly listed in CMake. Invalid KeyHeld<"Spcae"> and MousePressed<"Lfet"> separately fail compilation with the intended binding diagnostics.

Debug/Release Demo launch checks run from the actual bin directory, verifying copied assets, llvmpipe selection without a driver variable in the launching environment, logs at frameLimits 24 and normal window-close exit 0. The parent environment stays unchanged. The IDE debug button and manual controls were not exercised.

Actual framebuffer previews ecs-perspective.png, ecs-orthographic.png and ecs-wireframe.png were exported under ignored build/validation/ and visually inspected. They show the cube and bunny through component rendering with world axes. GoogleTest XML reports are windows-debug-lines.xml and windows-release-lines.xml in the same directory. The following sections record prior APIs and historical checks, superseded by the Entity/Assets implementation.

## Previous Mesh and Axis Gizmo validation

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
