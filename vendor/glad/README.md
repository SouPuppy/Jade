# Fixed OpenGL loader

These files are the reproducible GLAD 2.0.8 C output for OpenGL 3.3 Core,
with zero extensions and no built-in dynamic-library loader. Jade supplies
`glfwGetProcAddress` to `gladLoadGL` at context creation.

The build compiles `src/gl.c` directly. No code generator, package manager,
interpreter or GLAD download is part of configuration or compilation.

Preserve the generated SPDX notices, `LICENSE`, and the Khronos copyright
and permission notice inside `include/KHR/khrplatform.h` when distributing
these files. This directory is third-party code; do not reformat it.
