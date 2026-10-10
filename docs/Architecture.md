# Jade Architecture

## Modules and public boundaries

Jade uses C++17 and CMake. Windows x64 / MSVC is the current validation platform. The static engine target is exposed as `Jade::Jade`. Applications link that target; tests additionally link GoogleTest. GLFW, GLAD and system OpenGL are private dependencies. CMake passes static-library link dependencies to consumers without exposing third-party header search paths.

| Module | Public interfaces | Implementation responsibilities |
| --- | --- | --- |
| Core | Application, Log | Configuration, main loop, logging and application lifetime |
| Platform | Window, WindowConfig, Key | Window.cpp assembles the interface; GLFW manages native windows and runtime lifetime |
| Renderer | Renderer, Color | Renderer.cpp delegates clearing, context management and pixel validation to OpenGL |
| Demo | None | Create a configuration and application, then call Run() |
| tests | None | One GoogleTest smoke test |

Public headers live in `Jade/include/Jade/`; private headers and implementations live in `Jade/src/`. Sources are explicitly listed in CMake. Existing `<Jade/...>` paths, namespaces and C++ APIs remain available.

`Window`, `Renderer` and `Application` use PImpl. Renderer accesses the native window through a private interface; public headers contain no GLFW types or OpenGL declarations. Renderer currently delegates directly to OpenGL. There is no event framework, Layer, scene or Vulkan implementation.

## Lifetime

Window operations, input polling, context operations and drawing run on the main thread. The current design targets a single-window application and does not promise concurrent rendering across multiple windows or contexts.

`Application::Impl` owns Window before Renderer. Initialization creates the GLFW runtime and window, binds the OpenGL context, loads GLAD and checks the required 3.3 entry points. Destruction runs in reverse: unbind the context, destroy the window, then terminate GLFW. Runtime reference counting permits sequential and overlapping window lifetimes.

Invalid dimensions and window creation failures release already constructed runtime members. A failed OpenGLContext constructor unbinds the current context. A failed Application constructor destroys its window before returning an error.

The main loop polls events, handles Esc and close requests, then reads the actual framebuffer size. A zero-sized framebuffer skips drawing and waits for events. Otherwise Renderer sets the viewport, clears, optionally validates pixels and swaps buffers. `RunOptions` supports bounded frame runs and validation without exposing third-party rendering calls.

## Build and tests

The root CMake project enables C and C++, adds the engine and optional Demo, and adds root-level `tests/` when `BUILD_TESTING` is enabled. Test construction remains independent of Demo.

- `cmake/JadeBuildOptions.cmake` sets the shared CRT policy before project declaration, C++17 and project warning options.
- `cmake/JadeDependencies.cmake` declares pinned dependency archives and hashes, prepares the GLAD Python environment and adds GoogleTest only when needed.
- `cmake/JadeRuntime.cmake` detects optional Mesa files and provides a common deployment function.
- `cmake/DeployWindowsRuntime.cmake` copies or removes deployed Mesa DLLs on each executable build. A file lock serializes shared-output deployment.
- `tests/CMakeLists.txt` defines `JadeSmokeTests`. It runs directly, without CTest, batch files or PowerShell helpers.

GoogleTest 1.17.0 uses the same `/MDd` or `/MD` CRT as the engine. The single smoke test links `Jade::Jade` and `GTest::gtest_main`, creates a hidden window and validates three clear frames. Tests do not force Mesa version overrides or depend on machine-specific installation paths. Rendering checks need an OpenGL 3.3 context from either a system driver or optional Mesa.

The shared Ninja presets remain `windows-debug` and `windows-release`. Downloads, the Python environment, generated code and build outputs stay under `build/`. Visual Studio discovers the tool environment; command-line users run the same presets from an x64 developer shell.

Original optional runtime DLLs live under the ignored `runtime/mesa/x64/`. Disabling software rendering removes only deployed copies on the next build. No GLFW DLL is deployed.
