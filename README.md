# Jade

Jade is a C++17 static engine library built with CMake. The current validation platform is Windows x64 / MSVC. It provides a window, Esc and close-button exit, OpenGL 3.3 Core clearing, and a minimal Demo. Applications use `<Jade/...>` headers and the `Jade::Jade` target.

## Build

Install the Visual Studio or Build Tools **Desktop development with C++** workload, including MSVC x64, the Windows SDK, CMake and Ninja. GLAD generation also requires Python 3.9 or newer. The first configuration downloads pinned dependencies; subsequent configurations reuse each build directory's downloads and Python environment.

Open the repository folder in Visual Studio to discover the Windows Debug / Release presets and select `JadeDemo.exe` as the startup target. For command-line builds, use an **x64 Native Tools Command Prompt** or an x64 **Developer PowerShell**:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel
& ./build/windows-debug/bin/JadeSmokeTests.exe

cmake --preset windows-release
cmake --build --preset windows-release --parallel
& ./build/windows-release/bin/JadeSmokeTests.exe
```

GoogleTest runs directly through `JadeSmokeTests`; there is no CTest or batch-script entry point. Preset outputs remain `build/windows-debug/bin/` and `build/windows-release/bin/`, with static libraries under `lib/`. Machine-specific overrides belong in the ignored `CMakeUserPresets.json`.

All maintained files use two spaces for indentation, as configured in `.editorconfig`.

## Demo and build options

```powershell
& ./build/windows-debug/bin/JadeDemo.exe

# Build the engine and tests without Demo.
cmake --preset windows-debug -B build/no-demo-debug -DJADE_BUILD_DEMO=OFF -DBUILD_TESTING=ON
cmake --build build/no-demo-debug --parallel
& ./build/no-demo-debug/bin/JadeSmokeTests.exe
```

Demo creates an `ApplicationConfig`, constructs an `Application`, and calls `Run()`. Press Esc or close the window to exit. Hidden-window and three-frame pixel validation live in the tests. `JADE_BUILD_DEMO` and `BUILD_TESTING` independently control Demo and tests; disable both to build only the engine and its dependencies.

## Dependencies and runtime

[FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html) downloads GLFW 3.4, GLAD 2.0.8 and, when tests are enabled, GoogleTest 1.17.0. Every archive has a SHA-256 check. Dependency sources stay under each build directory's `_deps/`. All three libraries link statically. GLFW examples, tests, documentation and installation are disabled.

[GLAD's official CMake interface](https://github.com/Dav1dde/glad/blob/v2.0.8/cmake/GladConfig.cmake) generates OpenGL 3.3 Core with `REPRODUCIBLE` and `EXTENSIONS NONE`. Generated files stay under `build/<configuration>/generated/glad/`. The generator uses a separate `python/venv/` with Jinja2 3.1.6 and MarkupSafe 3.0.3. Download, environment creation and package-installation failures include the command output.

The engine, dependencies and consumers share the MSVC dynamic CRT: `/MDd` for Debug and `/MD` for Release. GoogleTest follows the [official shared-CRT configuration](https://google.github.io/googletest/quickstart-cmake.html).

The tests use engine public APIs and require an OpenGL 3.3-capable runtime. They do not require a particular Visual Studio installation path, Mesa installation, Windows message-injection script or preinstalled GoogleTest package. A graphics driver can provide OpenGL; Windows machines without one can optionally use the local Mesa DLLs described in [runtime/README.md](runtime/README.md).

Mesa files are ignored under `runtime/mesa/x64/`. On first configuration, detecting `opengl32.dll` enables `JADE_USE_SOFTWARE_RENDERING`; `libgallium_wgl.dll` must also be present. `JADE_MESA_RUNTIME_DIR` can point to another directory.

```powershell
# The next build removes deployed Mesa DLLs when software rendering is disabled.
cmake --preset windows-debug -DJADE_USE_SOFTWARE_RENDERING=OFF
cmake --build --preset windows-debug

cmake --preset windows-debug -DJADE_USE_SOFTWARE_RENDERING=ON
cmake --build --preset windows-debug
& ./build/windows-debug/bin/JadeSmokeTests.exe
```

For direct execution with Mesa, select llvmpipe in the current PowerShell session with `$env:GALLIUM_DRIVER = 'llvmpipe'`. See the runtime documentation for details.

Demo and tests share the same deployment rule. Disabling Mesa uses the system graphics driver. Original DLLs in `runtime/` remain untouched.

## Layout and tests

```text
Jade/
|-- CMakeLists.txt             # Project and subdirectories
|-- CMakePresets.json          # Windows x64 Debug / Release
|-- Jade/                     # Engine static library
|   |-- CMakeLists.txt
|   |-- include/Jade/         # Public interfaces
|   `-- src/                  # Private implementations
|-- Demo/                     # Example application
|-- tests/                    # One GoogleTest smoke test
|-- cmake/                    # Dependencies, build options and deployment
|-- runtime/mesa/x64/          # Ignored optional Mesa DLLs
|-- docs/                     # Architecture and validation records
`-- build/                    # Downloads, generated files and build outputs
```

Engine sources are listed explicitly in CMake. `Platform/Window.cpp` stays at the module root; GLFW implementation files stay in its subdirectory. Applications receive only public engine headers.

The single GoogleTest smoke test creates a hidden window, runs three frames and validates the clear pixels. Tests build independently of Demo.

See [Architecture](docs/Architecture.md) for module boundaries and [Validation](docs/Validation.md) for recorded results.
