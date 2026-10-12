cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build: cmake --preset windows-debug")
endif()

include(cmake/JadeBuildOptions.cmake)
project(Jade VERSION 0.1.0 LANGUAGES C CXX)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR
    "Jade requires a 64-bit Windows toolchain. The selected compiler/cache is 32-bit; "
    "the x64 preset label alone does not select an x64 cl.exe. Start an x64 Developer "
    "PowerShell and configure a fresh build directory, or clear the old CMake cache.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(BUILD_TESTING "Build the GoogleTest engine tests" ON)
option(JADE_BUILD_DEMO "Build the Demo example application" ON)
option(JADE_BUILD_BENCHMARKS "Build headless physics and ECS benchmarks" ON)
include(cmake/JadeDependencies.cmake)
include(cmake/JadeRuntime.cmake)

add_subdirectory(Jade)
if(JADE_BUILD_BENCHMARKS)
  add_executable(JadePhysicsBenchmark benchmarks/PhysicsBenchmark.cpp benchmarks/AllocationCounter.cpp)
  target_link_libraries(JadePhysicsBenchmark PRIVATE Jade::Jade)
  jade_configure_target(JadePhysicsBenchmark)
  add_executable(JadeEcsBenchmark benchmarks/EcsBenchmark.cpp benchmarks/AllocationCounter.cpp)
  target_link_libraries(JadeEcsBenchmark PRIVATE Jade::Jade)
  jade_configure_target(JadeEcsBenchmark)
endif()
if(JADE_BUILD_DEMO)
  add_subdirectory(Demo)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Untitled)
endif()
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
