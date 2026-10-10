# Jade Architecture

## Core model and boundaries

Entity identifies an existence in Scene. Components describe its state, systems interpret that state, and Assets owns shareable resources. The current foundation contains Scene/Entity, Transform, MeshRenderer, Camera and mesh assets. Physics, materials, light, prefabs and a general system scheduler are outside this implementation.

```mermaid
flowchart LR
  Application --> Scene
  Application --> Assets
  Scene --> Entity
  Entity --> Transform
  Entity --> MeshRenderer
  Entity --> Camera
  MeshRenderer --> MeshHandle
  MeshHandle --> Assets
  Assets --> Mesh
  Scene --> RenderSystem
  Assets --> RenderSystem
  RenderSystem --> Renderer
  Renderer --> OpenGL
```

| Public path | Contract |
| --- | --- |
| Core/Entity.hpp | Copyable identity, component access, validity/generation |
| Core/Scene.hpp | Entity/component ownership, destruction and snapshot queries |
| Core/Transform.hpp | Local position, quaternion rotation, scale |
| Assets/AssetHandle.hpp | Typed weak identity; MeshHandle alias |
| Assets/Assets.hpp | Immutable resource snapshots and synchronous OBJ cache |
| Math/Vector.hpp, Math/Quaternion.hpp, Math/Matrix4.hpp | Vectors, quaternion rotations and column-major matrices |
| Graphics/Mesh.hpp, Graphics/Vertex.hpp | Indexed CPU mesh, topology and builders |
| Graphics/MeshRenderer.hpp | MeshHandle and per-entity color |
| Graphics/Camera.hpp | Projection mode, FOV, height and clip planes |
| Graphics/Motion.hpp | Transform movement/rotation/directions and Camera zoom |
| Module/CameraControl.hpp | Scene-local free camera controls installed by Scene::use |
| Input/, Events/ | Typed input values, events and connection handles |
| Application.hpp | Active Scene selection, shared Assets, global events and main loop |
| Color.hpp, WindowConfig.hpp, Log.hpp | Shared values, window configuration and logging |
| Jade.hpp | Optional includes-only entry point |

Headers work independently. No folder aggregation header or legacy Object compatibility layer remains. Geometry consists of Mesh builders and Lines/Triangles topologies, all in 3D world space. Window/Renderer/FramebufferSize stay private. Consumer include paths expose neither backend headers nor implementation storage.

Each maintained .cpp includes its corresponding private .hpp first; CMake lists every source explicitly. Data-only headers need no empty .cpp. Core/Scene.hpp includes no graphics or platform dependency. Public template operations use type-erased bridges implemented in Core and Assets.

## Logging

The public `Jade/Log.hpp` exposes one `Jade::Log` API: Logger, Entry, Level, Config,
Record, Configure, SetLevel, Enabled, lowercase severity factories and Flush.
Each Logger owns a category name and shares output configuration; const insertion
reads that name without modifying the Logger.
`logger << info(...)` consumes an owning, move-only Entry; chaining is supported.
Move construction/assignment transfers the entry's ability to emit, and insertion
consumes it even if the current threshold filters it. Entries never emit from a
destructor. The previous Info/Error helpers and public Write API are removed
without compatibility wrappers or implicit default categories. C++20 format
templates bridge to `src/Core/Log.cpp`; backend state, locking and I/O stay private.

Logging uses process-wide configuration rather than Application ownership so GLFW
initialization errors, multiple windows and repeated Runs share the same policy.
Defaults enable terminal output from Debug upward in both build configurations; an append-only file and one
structured callback are optional. Application controls use category Application,
backend diagnostics use OpenGL/GLFW, and file loads/errors use Assets. Application
logs readiness and Run lifecycle at Info, with an Error record on exception abort.
Its constructor does not overwrite an explicit process-wide logging configuration.

EventDispatcher scopes its logger as Events/Application or Events/Scene:<id> for Debug subscription/cancellation records and
key/button edges. Trace covers Held, cursor, scroll and FrameUpdate. One shared
dispatch implementation preserves snapshots, exact matching and ordered
propagation while counting invoked handlers and identifying consumption by a
dispatcher-local subscription ID. Callback failures emit Error with binding and
exception reason before rethrowing the original exception. Cancellation records
are emitted after the next prune removes inactive slots; handles stay immediate
and noexcept. Logging never holds a live container iterator across a sink call.
Events diagnostics obey the global threshold independently of logPerformance.

ApplicationConfig.logPerformance replaces logFrames, defaults to true and controls
performance diagnostics. The allocation-free internal FrameDiagnostics aggregates
one-second windows and reports Debug summaries under Performance. It keeps frame
intervals (including pacing) separate from work wall time (held/update callbacks
through present, excluding the explicit pacing wait). GPU timing is not inferred.
Trace emits individual frames under Frame only when explicitly enabled. Hitches
exceeding max(100 ms, three target periods) emit at Warn at most once per second.
Each Run resets measurements; minimizing drops the previous interval and summary
window, excluding suspension and first-frame warmup. Scheduler counters remain
internal and tested rather than being printed on each frame.

Assets emits Debug load records only after successful cache publication. Errors
carry the path and exception message and are rethrown; path conversion for a log
cannot invalidate a successfully loaded asset or mask its original failure.

Output follows `HH:MM:SS.ffffff LEVEL [Logger] - message` using local time and
six-digit microseconds; severity is right-aligned to five columns. Console colors
are enabled only for native terminal streams, using GetConsoleMode/SetConsoleMode
on Windows and isatty on POSIX. Replaced C++ stream buffers, redirected files/pipes,
nonempty NO_COLOR and TERM=dumb disable color. Timestamps are gray and severity
colors reset before the category/message. File output always uses the plain format.

An atomic threshold avoids formatting disabled entries at creation. Entries format
and own their payload immediately; insertion rechecks the threshold, then captures
the record timestamp and Logger name. Filtered entries cannot be revived by a later
threshold change. A mutex serializes the
built-in outputs and configuration changes. A callback snapshot runs outside the
mutex; concurrent callbacks may overlap and arrive in a different order from file
lines. Per-thread recursion suppression permits callback logging without invoking
it again. Records own strings so consumers can copy them into a future editor
console. Sink/format/output exceptions are contained by the logging boundary;
configuration errors propagate before replacing state. Flush applies to built-in
outputs, and Error/Fatal flush the file immediately. Fatal does not abort.

The imported `.temp/log` implementation is reference material only. Its two APIs,
SDK bridge, destructor-driven emission, asynchronous queue, route tables, scope
propagation, subscription registries, queue metrics and runtime config parser are
not part of Jade. The synchronous design is sufficient for the current frame and
startup diagnostics; buffering can be added privately if measured I/O costs justify
it, with explicit queue capacity, overflow and shutdown policies at that time.

## Scene ownership, hierarchy and queries

Scene owns one private Registry, a scene dispatcher and a selected camera Entity. It is independently constructible, initially empty and noncopyable. Application retains a shared pointer to its active Scene; consumer references can keep inactive scenes alive. Entity stores a weak Registry reference and slot index/generation. Its 64-bit ID is unique within its Scene; equality also includes owner identity. Reuse increments generation and exhausted generations retire slots. Default, destroyed and expired handles cannot access data.

`scene.create()` makes an empty identity. Plain user structs and noncopyable components require no base. `add<T>(args...)` constructs a component; duplicate add throws logic_error, missing get throws out_of_range, and remove absent is a no-op. Invalid entity operations throw logic_error; has returns false. Const handles have mutable access. All operations use the main thread.

Component allocations survive unrelated creation and pool growth. Removal/destruction invalidates their references. Components are extracted before user destructors run so destructors can mutate the Scene. Constructors that destroy/reuse their identity cannot publish into the new generation.

`Scene::view<T...>()` is an iterable snapshot of weak identities. Advancing rechecks liveness and required components, skipping destruction, slot reuse and removed matches. New entities/matches wait for the next view. Views own their snapshots but do not keep Scene alive; iterators require their view to remain alive. Nested views are independent. Removing/replacing a component invalidates its references immediately.

Registry owns parent links and attachment-order children, exposed through Scene::setParent/parent/children. Parent changes validate liveness, ownership and cycles before mutation and retain local TRS. No Transform is required for an identity group; removing Transform preserves relationships. Destruction collects a complete subtree iteratively, invalidates every identity before any destructor, extracts all components, then releases them children before parents. Destructors can create/reuse slots safely without joining the already selected destruction set.

Scene::worldMatrix resolves full ancestor × local matrices. Rendering creates a fresh ancestor cache per pass, so direct component writes need no dirty flags. Full matrices preserve shear under nonuniform scale and rotation. Matrix4 is a public column-major value; construction/validation helpers remain private.

## Scene selection and events

Application owns Window, Renderer, shared Assets and its global dispatcher, with no implicit scene or camera. It selects a shared_ptr<Scene> via setScene; nullptr selects clear/present-only operation. Each Scene retains its own camera and dispatcher while inactive. The same Scene cannot be active or reserved in another Application.

Outside Run, selection commits immediately. During Run, one optional pending pointer records the last request, including unload. A processing pass holds one strong active pointer through input, update and render. At the next pass boundary it dispatches old SceneDeactivated, commits the pointer, then dispatches new SceneActivated. Reentrant lifecycle requests queue another selection rather than recurse. Selecting the current scene emits nothing.

Lifecycle Stop affects listeners only. Deactivation exceptions retain the old scene; activation exceptions retain the new one. The unused reservation and pending requests are released on failure and the original exception propagates. Input/update exceptions also discard queued switches. Normal bounded Run exit leaves a queued request for the next Run. Application destruction releases its ownership without invoking user lifecycle callbacks.

Ordinary events dispatch globally first, then to the retained active scene, then to unconsumed built-in Escape/F3 handlers. Stop prevents downstream propagation; consumed FrameUpdate still renders. Inactive scenes receive neither input nor update. Switches reset dt/diagnostic windows/pacing, with dt=0 on the first new frame, while Run elapsed and frame counts continue. Physical held keys/buttons remain intact; no synthetic transitions are introduced. Scene destruction expires its connections; Application destruction expires global connections.

## Assets

Assets owns typed immutable CPU allocations, with one-based resource IDs and weak typed handles. get returns const T&; add copies/moves a resource into an immutable snapshot. References survive later registrations. Handles do not extend owner lifetime. There is no unload or hot reload, so a nonempty handle remains valid until owner destruction. Owner identity rejects foreign, empty and expired handles. Generic user resources can be registered; Mesh is the only file-loaded type.

load<Mesh> resolves a canonical absolute path and synchronously parses OBJ. Repeated paths share one handle/allocation, including normalized relative aliases. Failure publishes neither a resource nor a cache entry; retry is allowed. Files changing after a successful load leave the cached immutable resource intact. Relative paths use the working directory; there are no hidden search paths.

Mesh owns vertices (position/normal), uint32 indices and Lines/Triangles topology. Registration checks finite data, index bounds, primitive grouping and supported draw counts. cube(size) emits 24 vertices/36 indices with hard normals; line(a,b) emits two Vec3 vertices; triangle(a,b,c) emits three vertices with a generated normal. sphere(radius, segments=32, rings=16) emits a closed indexed surface with smooth radial normals, shared poles and wrapped longitude indices; radius defaults to 0.5. Vertex/index limits are checked before allocation. Builtin cube and sphere generation lives under src/Graphics/builtins. OBJ parsing and mesh validation are internal, preserving positive/negative face references, convex triangulation, supplied normal seams and generated area-weighted normals. UVs, materials and smoothing groups are discarded.

## Rendering and camera

Scene::setCamera requires a live own-Scene identity with Transform and Camera. Creating Camera components never selects them implicitly. A removed/destroyed active camera causes clear/present only, retaining input and callbacks. Selecting a valid camera resumes geometry and axes.

RenderSystem is stateless. It iterates Scene::view<Transform, MeshRenderer>(), resolves meshes through shared Assets and submits Mesh plus DrawParameters. A per-pass SceneTransforms cache computes ancestor model, normal and rotation data. Renderer and OpenGL backend accept neither Entity nor Scene. Debug axes use three immutable internal line meshes and no Entities.

Coordinates are right-handed with Y up. A quaternion maps local +X right, +Y up and -Z front into parent coordinates. Local models are T*R*S; world models multiply the complete ancestry. Normal matrices compose inverse transposes of each local linear transform, preserving the full inverse transpose under inherited shear/nonuniform scale. The camera eye inherits the full parent matrix; orientation composes ancestor quaternions without scale and its own scale is ignored. View is the inverse of this rigid pose. Projection uses framebuffer aspect and NDC Z [-1,1].

Components are data aggregates. Consumers validate when used: finite positions and nonzero finite quaternions, positive model scales and dimensions, color channels in [0,1], FOV strictly (0,180), positive height/clipping planes and far>near. Both stored lens settings must remain valid, regardless of projection mode. Computed matrices outside float range are rejected. Invalid render data propagates an exception from Run, whose guard permits a later corrected Run.

The OpenGL mesh backend owns one normal-aware shader, VAO, streaming VBO and index buffer. It explicitly packs six position/normal floats per vertex. Indexed GL_LINES uses unlit color; GL_TRIANGLES uses ambient/directional diffuse light. Mesh data is streamed per draw; persistent GPU caching is deferred. BeginFrame clears color/depth, sets GL_LESS with depth writes, viewport and fill/wireframe. Each draw restores prior depth-test/write/function state. Gizmo lines disable depth test/writes to appear over meshes without changing polygon mode. When config.debug is enabled, F3 toggles positive unit XYZ axes colored red/green/blue. GL_LINE affects triangle edges only.

Motion operates on Transform; zoom operates on Camera. Translation preserves orientation; forward/right use current local axes and up/down use parent Y. lookAt replaces the quaternion using target/up in parent coordinates once. Rotation composes normalized quaternion increments about current local axes, with rightward yaw, upward pitch and clockwise roll. There is no pitch clamp or Euler decomposition, so full flips and yaw/pitch after roll remain consistent with view/direction calculations. Zoom bounds the active lens parameter without changing pose/mode/clip planes; changing projection preserves both zoom values.

## Scene modules

Scene::use<T>() invokes static void T::attach(Scene&) once per type and Scene. The private ledger marks installations in progress to reject same-type recursion. Installation checkpoints the scene dispatcher subscription IDs; failure cancels subscriptions created since that checkpoint and removes new nested ledger entries. Existing module registrations survive, and failed installs can be retried. The rollback covers subscriptions and the installation ledger only; arbitrary module side effects remain the module's responsibility. Module types need no constructor, and there is no unload API.

Module::CameraControl lives in the engine and registers scene-local motion, left-button dragging, zoom and lifecycle callbacks. Shared callback state keeps a Scene reference and weak Entity identity without retaining the Scene. Every input resolves the current selected camera and skips missing components. A changed target or activation/deactivation resets drag state. Demo installs this module in each scene and retains only scene construction, animation and scene-selection shortcuts.

ApplicationConfig.debug defaults to false independently of build type and logging. When enabled, unconsumed unmodified Pressed F/F3/O toggle wireframe, axes and the retained scene camera's projection after both dispatcher layers. O checks Transform and Camera and retains pose/zoom/clips. stop or a closed window prevents these actions. Renderer owns wireframe state and its getter backs Application::wireframeEnabled(); no second state flag is cached. Initial axes still follow config.axisGizmo independently of debug.

## Loop, events and build

After committing any pending scene, the main loop polls and dispatches ordered key/button/cursor/scroll events, applies unconsumed Escape and enabled F/F3/O defaults, waits for steady-clock deadlines, dispatches held keys/buttons and FrameUpdate, then clears, renders, presents and logs. Zero-sized framebuffers keep processing edge input but skip held/update/render/logs; restoration resets dt. FrameClock skips expired slots after overruns. frameLimits defaults to 24 FPS; RunOptions::frameLimit independently caps submitted frames. Each Run retains scene/resources/subscriptions and resets timing. stop allows a later Run; nested Run throws.

[Events](Events.md) describes propagation, exact modifiers and connection lifetime. CMake explicitly lists source/header pairs. Demo and tests are independently optional. GLFW/GLAD are private static dependencies; GoogleTest is configured only for tests. Windows Debug/Release use matching /MDd and /MD runtimes. Software builds select llvmpipe before GLFW when no nonempty driver override exists, and release GPU resources with the owning context current. Runtime/asset deployment stays shared across target configurations.

Tests cover module installation/rollback/lifetime, opt-in debug shortcuts and camera-control targeting/reset, Scene lifetime/generations, hierarchy, mutation snapshots, deferred switching and lifecycle failures, Assets sharing/cache/lifetime, quaternion direction/view agreement, camera lifecycle, input ordering, timing and real OpenGL pixels/depth for both topologies, projections, normals, resizing, clipping, wireframe and axes. Local screenshots and Demo launch checks stay under ignored build/validation/.
