#include "GlfwRuntime.hpp"

#if defined(_WIN32) && JADE_USE_SOFTWARE_RENDERING
#include <windows.h>
#include <system_error>
#endif

#include <GLFW/glfw3.h>

#include <cstdio>
#include <stdexcept>

namespace Jade::Detail {
namespace {

unsigned int runtimeUsers = 0;

void ConfigureRenderingDriver()
{
#if defined(_WIN32) && JADE_USE_SOFTWARE_RENDERING
  // Select Mesa's CPU driver before GLFW creates any OpenGL contexts.
  // Keep an explicit driver choice supplied by the caller.
  if (GetEnvironmentVariableW(L"GALLIUM_DRIVER", nullptr, 0) == 0
      && !SetEnvironmentVariableW(L"GALLIUM_DRIVER", L"llvmpipe")) {
    throw std::system_error(static_cast<int>(GetLastError()),
      std::system_category(), "Failed to select Mesa's llvmpipe driver.");
  }
#endif
}

void OnGlfwError(int code, const char* message) noexcept
{
  std::fprintf(stderr, "[Jade] GLFW %d: %s\n", code,
    message ? message : "Unknown error");
}

} // namespace

GlfwRuntime::GlfwRuntime()
{
  if (runtimeUsers == 0) {
    ConfigureRenderingDriver();
    glfwSetErrorCallback(OnGlfwError);
    if (!glfwInit()) {
      glfwSetErrorCallback(nullptr);
      throw std::runtime_error("Failed to initialize GLFW.");
    }
  }
  ++runtimeUsers;
}

GlfwRuntime::~GlfwRuntime()
{
  if (--runtimeUsers == 0) {
    glfwTerminate();
    glfwSetErrorCallback(nullptr);
  }
}

} // namespace Jade::Detail
