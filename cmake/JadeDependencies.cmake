include(FetchContent)
# Keep fetched dependency sources and downloads in this build directory.
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")
set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)
set(BUILD_SHARED_LIBS OFF)
set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
  URL https://codeload.github.com/glfw/glfw/tar.gz/refs/tags/3.4
  URL_HASH SHA256=c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(glfw)

# Fixed GLAD 2.0.8 output: OpenGL 3.3 Core, no extensions or bundled loader.
# Compile the checked-in C source; no generator or interpreter is required.
set(jade_glad_root "${CMAKE_CURRENT_LIST_DIR}/../vendor/glad")
add_library(jade_glad STATIC
  "${jade_glad_root}/src/gl.c"
  "${jade_glad_root}/include/glad/gl.h"
  "${jade_glad_root}/include/KHR/khrplatform.h")
target_include_directories(jade_glad PUBLIC "${jade_glad_root}/include")
find_package(OpenGL REQUIRED)

if(BUILD_TESTING)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(gtest_build_tests OFF CACHE BOOL "" FORCE)
  set(gtest_build_samples OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    URL https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.17.0
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  FetchContent_MakeAvailable(googletest)
endif()
