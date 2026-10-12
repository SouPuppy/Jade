# Untitled — World Streaming showcase

Untitled v0.1 validates Jade's World Runtime with a 256x256-metre authored world. It contains 16x16 Chunks, each 16 metres square with 4x4 grass cells. The character begins in Chunk (8,8); streaming defaults to 25 data/render/static-collision Chunks and 9 simulated Chunks. Counts are clipped at map edges.

## Build and run

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --parallel
& ./build/windows-release/bin/Untitled.exe
& ./build/windows-release/bin/Untitled.exe --pipeline hybrid --frames 120 --hidden
```

CMake deploys Assets to `UntitledAssets` and authored World data to `UntitledWorld` beside the executable. Saves go to `UntitledSaves` there. The game reads WorldConfig and Chunk JSON rather than generating terrain during play.

| Control | Action |
| --- | --- |
| WASD | Camera-relative movement |
| Q / E | Rotate the view by 45 degrees |
| F | Push the nearest active ball within five metres |
| X | Delete that ball and persist its tombstone |
| R | Reset player/camera; retain saved object changes |
| Esc | Exit; in observer mode, leave that mode first |
| I | Cycle Raster / Hybrid / RayTracing / None |
| F1 / F3 / F4 / F5 | Wireframe / bounds / physics / normals |
| F2 | Independent observation camera |
| P / O | Pause / change projection |

The HUD reports current Chunk, data/render/collision/active counts, in-flight loads, frozen dynamic objects, save status and errors. Movement waits for collision readiness. The character is a SpriteRenderer + Animator + Collider + CharacterController entity without a RigidBody.

## Geometry and persistence

Grass uses analytic finite plane children with alternating material colors. A static thin Box per Chunk supplies ground collision. Ball groups demonstrate low polygon, analytic sphere and high polygon geometry with the same radius and Collider. Dynamic balls outside simulation demand freeze and stop colliding; static floors remain available across collision demand.

F applies a linear/upward impulse. X removes the persistent object. Position, rotation, velocities and visual representation survive unload/reload and process restart. One atomic World snapshot transfers cross-Chunk ownership without duplicating the authored ball. Normal exit waits for the final save; load/save errors are surfaced rather than silently replacing saved state. Each save directory has one writer.

## Rendering and assets

Raster is the startup pipeline at a 24 FPS presentation target. Hybrid uses raster geometry with CPU/GPU shadow rays. RayTracing supports CPU/GPU transport, with GPU selected by default, half-resolution film and four bounces. Initialization failure falls back to CPU. Supported CLI options are `--pipeline`, `--device`, `--scale`, `--frames`, `--hidden`, `--soul` and `--benchmark`; `--help` lists values.

Analytic geometry is exact for ray visibility and traced emission sampling; raster/debug use cached tessellation. Raster/Hybrid direct lighting and dielectric appearance are approximations; they do not trace reflection, refraction or indirect light. None retains the window and independent diagnostics.

F2 keeps the World running while WASD, Space/Shift, right-drag look, middle-drag pan and wheel zoom control a separate observer. The original camera and sprite orientation remain the gameplay view. Diagnostic paths sample that game camera on CPU at a bounded rate.

Assets include the bundled Hormelz character archives and source sheets. Runtime hero clips use nine Walk and fourteen ReadyIdle frames per direction, at the existing 12 FPS timing fallback. Nearest sRGB texture sampling and alpha cutout are shared across rendering paths.

Architecture and lifecycle details: [World Runtime](../../docs/WorldRuntime.md). Measured automated checks: [Validation](../../docs/Validation.md). Automated callback/readback tests and hidden launches establish behavior and startup, not a manual interaction or hardware-independent performance guarantee.
