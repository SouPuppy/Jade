include(FetchContent)
# Keep dependency sources, downloads and generated code in this build directory.
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
FetchContent_Declare(glad
  URL https://codeload.github.com/Dav1dde/glad/tar.gz/refs/tags/v2.0.8
  URL_HASH SHA256=44f06f9195427c7017f5028d0894f57eb216b0a8f7c4eda7ce883732aeb2d0fc
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  SOURCE_SUBDIR cmake)

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(_jade_python_root "${CMAKE_BINARY_DIR}/python")
set(_jade_venv "${_jade_python_root}/venv")
if(WIN32)
  set(_jade_python "${_jade_venv}/Scripts/python.exe")
else()
  set(_jade_python "${_jade_venv}/bin/python")
endif()
file(MAKE_DIRECTORY "${_jade_python_root}/tmp")
function(jade_python_command description)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "TMP=${_jade_python_root}/tmp"
      "TEMP=${_jade_python_root}/tmp"
      "TMPDIR=${_jade_python_root}/tmp"
      "PYTHONNOUSERSITE=1" "PYTHONDONTWRITEBYTECODE=1"
      ${ARGN}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "${description} failed (${result}).\n${output}\n${error}")
  endif()
endfunction()
if(NOT EXISTS "${_jade_python}")
  message(STATUS "Creating GLAD Python environment: ${_jade_venv}")
  jade_python_command("GLAD Python virtual environment creation"
    "${Python3_EXECUTABLE}" -m venv "${_jade_venv}")
endif()
execute_process(COMMAND "${_jade_python}" -c
  "import sys, importlib.metadata as m; assert sys.version_info >= (3,9); assert m.version('Jinja2') == '3.1.6'; assert m.version('MarkupSafe') == '3.0.3'"
  RESULT_VARIABLE _jade_packages_ready OUTPUT_QUIET ERROR_QUIET)
if(NOT _jade_packages_ready STREQUAL "0")
  message(STATUS "Installing pinned GLAD templates: Jinja2 3.1.6, MarkupSafe 3.0.3")
  jade_python_command("GLAD template dependency download/install"
    "${_jade_python}" -m pip install --disable-pip-version-check
    --cache-dir "${_jade_python_root}/pip-cache"
    --only-binary=:all: Jinja2==3.1.6 MarkupSafe==3.0.3)
endif()
# GLAD's official interface uses FindPython, not FindPython3.
set(Python_EXECUTABLE "${_jade_python}")
FetchContent_MakeAvailable(glfw glad)
glad_add_library(jade_glad STATIC REPRODUCIBLE API gl:core=3.3 EXTENSIONS NONE
  LOCATION "${CMAKE_BINARY_DIR}/generated/glad")
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
