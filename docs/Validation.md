# Windows x64 Validation

## Current test entry point

The project now uses GoogleTest 1.17.0 directly. The single smoke test lives under root-level `tests/` and runs through `JadeSmokeTests.exe`. CTest registration, test presets and the PowerShell window-message helper have been removed.

The smoke test creates a hidden window and validates three clear frames. Tests use the engine public API and do not depend on a local GoogleTest installation or Mesa-specific environment variables. Rendering checks require OpenGL 3.3.

Demo remains a minimal configuration, application and `Run()` entry point. Esc and the native close button remain application behavior; automated input injection is no longer part of the test suite. The old Mesa 2.1 fault-injection test, lifecycle test groups and standalone consumer fixtures have been removed to keep the suite minimal.

Validated on 2026-10-10 after simplifying the suite and restoring root-level `tests/`:

| Configuration | Build | GoogleTest |
| --- | --- | --- |
| windows-debug | Passed | 1/1 passed |
| windows-release | Passed | 1/1 passed |
| Demo disabled, Debug | Passed | 1/1 passed |
| Demo disabled, Release | Passed | 1/1 passed |

These runs used optional Mesa with `GALLIUM_DRIVER=llvmpipe` selected in the validation shell. The test itself sets no driver environment variables. Mesa's default D3D12 selection on this machine failed the pixel check; llvmpipe passed. GoogleTest XML reports are saved under the ignored `build/validation/logs/` directory as `<configuration>-gtest.xml`.

The maintained source, CMake and configuration files passed two-space indentation checks. Documentation is English, and the root test directory contains only `CMakeLists.txt` and `SmokeTests.cpp`.

## Previous migration validation

The migration was validated on 2026-10-10 with MSVC 19.51.36260, CMake 4.3.1-msvc1, Ninja 1.13.2 and Python 3.14.8. GLAD used its isolated environment with Jinja2 3.1.6 and MarkupSafe 3.0.3. GLFW 3.4 and GLAD 2.0.8 were fetched from pinned archives; GLAD generated OpenGL 3.3 Core with REPRODUCIBLE and no extensions. Rendering used Mesa 26.2.4 / llvmpipe.

Before replacing the test runner, Debug and Release each passed five CTest entries. Builds with Demo disabled each passed three entries, and the standalone consumer passed its own rendering check. These are historical results for the earlier test setup.

The migration also checked engine-only builds, Mesa deployment and removal, static GLFW / GLAD linkage, matching `/MDd` and `/MD` runtime flags, and absence of generated files from maintained source directories. The consumer included public headers and linked only `Jade::Jade`; compilation rejected leaked GLFW or GLAD header search paths.

Visual Studio's CMake File API exposed the `JadeDemo` target and `bin/JadeDemo.exe` output. Program execution was validated through test processes; the IDE debug button was not manually exercised.

Old builds, dependency installations, configuration and removed validation scripts were archived under `.local/` during the migration. Existing backups were retained. No Git staging, commits, resets or history edits were performed by the migration work.
