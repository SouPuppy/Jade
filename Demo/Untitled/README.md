# Untitled — HD-2D prototype

Untitled is a playable game on Jade. It uses a 3D ground, an eight-direction animated
sprite and the engine's interchangeable Raster, Hybrid and CPU/GPU RayTracing pipelines. All character resources live in this Demo.
The existing engine scenes remain available through `JadeDemo` in `Demo/Showcase`.

## Build and run

Use an x64 Visual Studio Developer PowerShell:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --target Untitled --parallel
& ./build/windows-release/bin/Untitled.exe
```

The build copies `Assets` to `bin/UntitledAssets`. Resources are located relative to
the executable, so launching from Visual Studio or another working directory works.
No download, Python, Aseprite installation or network access is required to play.

| Key | Action |
| --- | --- |
| WASD | Move relative to the camera; diagonal movement has the same speed |
| Q / E | Rotate the camera by 45°, once per press |
| R | Reset player, camera and animation |
| Esc | Leave soul mode; otherwise exit |
| P | Pause gameplay, physics and animation; camera rotation remains available |
| I | Cycle Raster ? Hybrid ? RayTracing ? None ? Raster |
| F1 | Toggle mesh wireframe over the selected pipeline |
| F2 | Enter/leave the soul observation camera; the world keeps running |
| F3 | Toggle scene axes/AABBs, entity axes, BVH, camera frustums and light directions |
| F4 | Toggle collision, contact and physics debug drawing |
| F5 | Switch color / normal output without changing the selected pipeline |
| O | Switch orthographic / perspective projection |

Engine debug shortcuts and Debug logging are enabled by default. Debug overlays
start hidden. The game initially uses an orthographic camera with a 45° pitch.

## Rendering and animation

The window is 960×640 with a 24 FPS cap. GPU PathTrace defaults to a 0.5 resolution
scale (480×320), at least one complete sample per frame and four bounces. Remaining
frame time can be used for more samples. Frames do not accumulate old character poses.
GPU initialization failure is logged and falls back to CPU. Hybrid CPU shadows use
parallel CPU queries and a visibility mask at the configured resolution scale; raster
geometry remains at window resolution. Actual performance is
hardware-dependent.

```powershell
& ./build/windows-release/bin/Untitled.exe --device cpu --scale 0.25
& ./build/windows-release/bin/Untitled.exe --device gpu --scale 0.375
& ./build/windows-release/bin/Untitled.exe --benchmark --hidden --frames 1441
```

`--pipeline raster|hybrid|tracing|none` selects the startup pipeline. `--soul` starts
in soul mode. `--device` selects the ray execution backend, including Hybrid shadows.

`--benchmark` supplies eight movement directions and rotates the camera every four
seconds. Without `--frames`, it runs 1441 frames, approximately one minute at 24 FPS.
The engine logs FPS, frame interval, work duration and sample duration once per second.
`--help` lists all options. `--hidden` is for automated checks.

Walk has nine active 256×256 frames in a 4×3 sheet; ReadyIdle has fourteen in a 4×4
sheet. Empty trailing cells are excluded. Source timing metadata was not provided,
so clips use 12 frames/second independently of the display cap. Idle/Walk transitions
restart the clip; direction changes retain phase. The character's world-facing direction
is retained when movement stops, and camera rotation changes the displayed direction.

Sprite texture sampling is nearest-neighbor in sRGB converted to linear RGB.
Alpha below 0.5 is discarded for primary, indirect and shadow rays. Sprites remain
upright, use a shared quad and display unlit texture colors while casting silhouette
shadows. Animation only changes the frame region; it does not rebuild mesh acceleration.

The planar CharacterController moves against static Box/Sphere Colliders without a
RigidBody. This first version has a fixed ground height and does not implement gravity,
jumping, steps, slopes or rigid-body pushing. Gameplay state mapping stays in Untitled.

## Soul observation mode

F2 creates a Runtime-owned free camera without editing the game's selected camera.
WASD flies, Q/E moves down/up, Shift accelerates, and holding the right mouse button
turns the view. These controls are consumed before gameplay input. O changes only the
observer projection. The game, physics and animation continue; P still controls pause.
F2 or Esc returns to the selected game pipeline and restores mouse capture. I changes
the pipeline to use after leaving the observer.

The observer uses a raster background with the game camera frustum and a 16?9 grid
of diagnostic transport samples from the game camera. Paths refresh at most 6 Hz and
have at most four bounces. Cyan shows primary segments, orange bounces, green visible
shadow tests, red blocked shadow tests, and gray escaped rays. They use the CPU path
integrator's actual intersection/material sampling, not the observer's view rays or
an exact replay of GPU image samples. Each refresh uses the latest world snapshot.
Scene switches clear observer state. None still allows debug overlays and this observer;
it does not create a headless server.

## Assets and verification

`Assets/Characters/Hero/Archives` contains all thirteen original RAR files;
`Original` contains all 104 extracted PNGs. `manifest.json` records source, dimensions,
hashes, direction order and runtime defaults. Only Walk and ReadyIdle are used in phase one.

Run `JadeSmokeTests.exe` to exercise engine and game tests. The Untitled tests validate
all sixteen runtime sheets, movement at every camera orientation, animation/state
transitions, keyboard repeat, focus loss, pause, reset and debug shortcuts. Engine tests
check alpha visibility, shadow queries, CPU/GPU agreement, billboard anchoring and collision.
The preview test also writes `build/<preset>/validation/untitled-preview.ppm`.
