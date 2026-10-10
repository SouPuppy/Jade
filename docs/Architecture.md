# Jade Architecture

## Public and internal boundaries

Jade is a C++20 static library exposed as `Jade::Jade`. Windows x64 / MSVC is the validated platform. Consumers receive only `Jade/include/`; GLFW, GLAD and system OpenGL remain private dependencies.

| Public header | Responsibility |
| --- | --- |
| Application.hpp | ApplicationConfig, RunOptions, ownership, frame loop, active camera, wireframe and axes |
| Input/Key.hpp | Backend-independent keys, modifier masks and compile-time chord parsing |
| Input/MouseButton.hpp | Backend-independent buttons and compile-time name parsing |
| Events/Event.hpp | EventConnection and EventFlow propagation result |
| Events/Keyboard.hpp | KeyPressed, KeyReleased and KeyHeld string-template events |
| Events/FrameUpdate.hpp | Per-frame dt, elapsed time and frame index |
| Events/MouseMoved.hpp | Window cursor position and relative movement |
| Events/Mouse.hpp | MousePressed, MouseReleased and MouseHeld string-template events |
| Events/MouseScrolled.hpp | Horizontal and vertical scroll offsets |
| Vector.hpp | Vec2/Vec3 arithmetic, dot and cross; Vec2D/Vec3D aliases |
| Objects/Camera.hpp | Perspective/Orthographic modes and look-at settings; inherits Object |
| Objects/Motion.hpp | Camera translation, yaw/pitch/roll increments and projection zoom |
| Objects/Object.hpp | Owned mesh, color, position, rotation and scale for 3D objects |
| Mesh/Mesh.hpp | Indexed CPU triangle mesh, builtin cube and OBJ loading |
| Mesh/Vertex.hpp | Local position and surface normal |
| Geometry/Line.hpp | One screen-space segment with two owned 2D endpoints |
| Geometry/Triangle.hpp | Three owned screen-space 2D vertices and color |
| Color.hpp | RGBA value |
| WindowConfig.hpp | Title, dimensions and visibility |
| Log.hpp | Info and Error logging |
| Jade.hpp | Optional includes-only entry point |

Each public header can be included independently. Source files explicitly list their dependencies and include their corresponding `.hpp` first. Vec2 and Vec3 are header-only value types, so they have no redundant implementation files.

```text
Jade/
|-- include/Jade/              # Core headers and Jade.hpp
|   |-- Geometry/              # Line and Triangle
|   |-- Mesh/                  # Mesh and Vertex data
|   |-- Input/                 # Key, chord and mouse button contracts
|   |-- Events/                # Event types and connections
|   `-- Objects/               # Object, Camera and Motion
|-- src/Core/                 # Application and Log implementations
|-- src/Geometry/             # Line and Triangle implementations
|-- src/Objects/              # Object, Camera and Motion implementations
|-- src/Mesh/                 # Mesh API and builtins/Cube generation
`-- src/internal/
    |-- Core/FrameClock.hpp
    |-- Events/               # Callback storage and dispatch
    |-- Input/                # Key/button state and ordered input queue
    |-- Math/                 # Matrix4 and validation helpers
    |-- Mesh/                 # OBJ parser and mesh validation contract
    |-- Platform/             # Window facade and GLFW runtime/window
    `-- Renderer/             # Renderer facade and OpenGL backend
```

Window, FramebufferSize and Renderer are internal. Key and MouseButton are public input values. ApplicationConfig retains a public WindowConfig for application settings. Tests privately opt into the source directory and GLFW/GLAD for pixel readback; those paths are not exported to consumers.

## Ownership and lifetime

Application, Window and Renderer use PImpl. All creation, mutations and rendering belong to the main thread. The application owns Window before Renderer, with objects held in separate vectors of `unique_ptr`; references remain stable across subsequent creation.

A default Camera is available through `app.camera()`. `create<Camera>()` activates the new camera while retaining previously created cameras. Geometry, Mesh and Objects hold no GPU resources. `create<Object>()` creates a renderable world object; `setMesh()` validates and takes an owned snapshot, and `GetMesh()` returns const access. Empty meshes draw nothing and meshes may be replaced between frames. Camera inherits Object and shares its transform storage; Line and Triangle remain independent 2D geometry. Object has a virtual destructor and virtual `setPos()` so Camera validation applies when moved through an Object reference.

Before GLFW initialization, software rendering builds choose llvmpipe only if the caller has not supplied a nonempty GALLIUM_DRIVER. Hardware builds leave the environment untouched. GLFW requests OpenGL 3.3 Core and a depth buffer. OpenGL resources are created with the context current and released before that context and its window are destroyed. Shader compilation/link failures report driver logs and clean up partial allocations.

Invalid configuration is rejected before creating a window. Geometry, camera and transform setters validate before replacing state. Positions and rotation must be finite; dimensions, scale, visible heights and near/far distances must be positive; far must exceed near. Color components are finite and in [0, 1]. Computed matrices that exceed float range are rejected before upload.

## Rendering and coordinates

The primitive renderer owns a shader program, VAO and streaming VBO for 2D shapes and world axes. Its vertices contain explicit position/color floats. The mesh renderer owns a separate normal-aware shader, VAO, streaming VBO and index buffer, using `glDrawElements(GL_TRIANGLES, ..., GL_UNSIGNED_INT, ...)`. It explicitly packs position/normal floats; neither renderer assumes C++ struct padding. OpenGLProgram shares shader compilation, linking and cleanup across both implementations. Mesh data is uploaded on each draw; persistent per-mesh GPU caching is a future optimization.

The 2D overlay keeps the lower-left origin and a default visible height of 10 units. Width follows framebuffer aspect ratio. Line draws GL_LINES with two vertices, Triangle draws GL_TRIANGLES with three vertices; coincident lines and collinear triangles are skipped. Depth testing is disabled for overlays.

The 3D world is right-handed with Y up. Camera defaults to eye (3, 3, 5), target (0, 0, 0), and up (0, 1, 0). Perspective uses vertical FOV in degrees, default 60; orthographic uses visible height, default 6. Both use near 0.1 and far 100. Orthographic retains size with distance; perspective applies the homogeneous divide. FOV is meaningful in perspective mode, matching [the documented vertical-FOV convention](https://docs.unity.com/en-us/engine/6000.3/script-reference/unityengine/camera/fieldofview).

`Mesh::cube(size)` creates 24 vertices and 36 indices for six flat faces centered at the local origin. Size is baked into positions; Object scale, rotation and translation apply once to any mesh. Shared corners have separate face normals. Object color supplies a base tint. The fragment shader applies fixed ambient and directional diffuse lighting; normals use the inverse transpose of the model's linear transform, including nonuniform scale. The vertex shader uses:

```text
clipPosition = projection * view * model * localPosition
model = translation * rotationZ * rotationY * rotationX * scale
```

Matrices use column-major storage and are uploaded with GL_FALSE, as described by [OpenGL glUniform](https://wikis.khronos.org/opengl/GLAPI/glUniform). View uses an orthonormal look-at basis and rejects coincident eye/target or parallel up. Camera's inherited rotation supplies a local offset from that basis; `lookAt()` resets it. View inverts that rotation and the inherited scale. Projection maps the near/far clip planes to NDC Z [-1, 1].

Every frame clears color and depth, draws Objects with GL_LESS depth testing and writes enabled, then draws the Axis Gizmo, followed by 2D Triangles and Lines. The gizmo consists of three positive XYZ segments at the world origin, each one unit long, colored red/green/blue. It uses the active camera, draws over world objects without writing depth, and restores depth-test/write state. ApplicationConfig::axisGizmo defaults to true; unconsumed bare F3 presses toggle it once and log on/off. Public setter/query methods permit application control.

`app.setWireframe()` forwards to the private renderer; each BeginFrame applies GL_LINE or GL_FILL to polygon faces. It defaults to fill, leaves GL_LINES unchanged, and includes each mesh's triangle edges. The viewport and aspect ratio use actual framebuffer size. Resizing keeps vertical FOV or orthographic visible height fixed while adjusting horizontal coverage.

## Mesh loading

Mesh exposes vectors of Vertex (position/normal) and uint32 indices. Mesh::load delegates OBJ
parsing to src/internal/Mesh/ObjLoader; builtin generation lives under src/Mesh/builtins.
Validation rejects nonfinite vertices, incomplete triangle index groups, out-of-range references
and counts outside the draw API's limits before Object state changes.

OBJ supports positive/negative face references and position/UV/normal syntax, triangulates convex
polygons, normalizes supplied normals and splits vertices at explicit normal seams. Missing normals
use area-weighted averages across shared source positions; degenerate-only vertices retain zero
normals and receive ambient light. UV data, smoothing-group directives and materials are discarded.
Concave polygons require prior triangulation. Errors carry a filename and line number. Mesh::load
resolves the supplied path from the working directory without hidden search paths.

assets/bunny.obj preserves the smallest supplied PLY's 453 positions and 948 triangles. Demo
scales/translates the Object rather than rewriting geometry. Source attribution and terms live
in [assets/README.md](../assets/README.md). CMake's always-run JadeDemoAssets target copies assets
beside the executable, independently of engine/tests builds.

## Frame loop and build

The loop polls events, dispatches queued key/button transitions, cursor movement and scroll input in arrival order, handles unconsumed Escape/F3 and close, waits for the next steady-clock deadline, dispatches held keys, held buttons and FrameUpdate, then clears, draws and presents. Zero-sized framebuffers keep processing queued input but skip held/frame callbacks, drawing and logs; restoration starts with zero dt. FrameClock skips expired deadlines after overruns. ApplicationConfig::frameLimits defaults to 24 FPS; RunOptions::frameLimit independently limits total submitted frames. Every Run resets timing statistics and retains objects, cameras and subscriptions. An explicit stop bypasses a bounded run's remaining frame requirement.

CMake explicitly lists source/header pairs. Demo and tests are independently optional. GLAD/GLFW are static and private; GoogleTest is only configured for tests. All targets share /MDd in Debug and /MD in Release. Mesa deployment is shared between Demo and tests and serialized with a file lock.

The event pipeline and callback/lifetime contract are described in [Events](Events.md). Camera setPos translates its look-at target along with the eye; getFront/getRight/getUp derive normalized world axes from the same look-at basis and yaw(Y) * pitch(X) * roll(Z) rotation used by its view transform. This lets roll bank the camera without changing its viewing direction, including after yaw/pitch. Object meshes use rotationZ * rotationY * rotationX. Motion delegates translation to camera axes, except up/down along world Y. Its yaw/roll increments wrap and pitch is limited to [-89, 89] degrees.

Sixty-three GoogleTests cover public data, ownership, camera configuration, mesh validation, OBJ loading, normals, deterministic timing/matrix checks, keyboard/mouse dispatch, Motion and real pixel/depth readback. Mesh pixel checks include indexed selection, geometry replacement, normal-driven lighting and bunny rendering in both projections and wireframe. Axis checks cover unit length, camera changes, resize, depth-state preservation and F3 propagation. Input integration tests invoke installed GLFW adapters on hidden windows. Local screenshots and Demo launch checks remain under ignored build/validation/.