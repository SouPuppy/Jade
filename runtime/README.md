# Optional Local Runtime

GLFW 3.4 and GLAD 2.0.8 are downloaded by CMake FetchContent and linked statically. Their DLLs are not required. This directory documents the optional software renderer; local Mesa DLLs are ignored by Git.

`mesa/x64/` can contain the OpenGL files `opengl32.dll` and `libgallium_wgl.dll`. When software rendering is enabled, the shared deployment rule copies them beside Demo and the test executables. Disabling it removes deployed copies on the next build and keeps the originals.

Source: [mesa-dist-win 26.2.4](https://github.com/pal1000/mesa-dist-win/releases/tag/26.2.4)

Archive: `mesa3d-26.2.4-release-msvc.7z`

Archive SHA-256: `351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695`

To reproduce this runtime, download that MSVC archive, verify its checksum and copy the two x64 DLLs into `runtime/mesa/x64/`. See the release's instructions and the [Mesa license](https://docs.mesa3d.org/license.html).

On first configuration, CMake enables `JADE_USE_SOFTWARE_RENDERING` if it detects `opengl32.dll`; the second DLL must also exist. Later configurations use the cached option, which can be set explicitly to ON or OFF. `JADE_MESA_RUNTIME_DIR` selects an alternative directory.

Mesa is optional. Machines with a graphics driver supporting OpenGL 3.3 can build and run Demo and the tests without these files. Test code does not reference local DLL paths or force Mesa-specific environment settings.

To select Mesa's llvmpipe software driver when running an executable directly from PowerShell, set `$env:GALLIUM_DRIVER = 'llvmpipe'` in that shell. Visual Studio's debug environment sets this variable when software rendering is enabled. This is a runtime choice and is not part of the test code.
