#include "GlfwRuntime.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <stdexcept>

namespace Jade::Detail {
namespace {

unsigned int runtimeUsers = 0;

void OnGlfwError(int code, const char* message) noexcept
{
  std::fprintf(stderr, "[Jade] GLFW %d: %s\n", code,
    message ? message : "Unknown error");
}

} // namespace

GlfwRuntime::GlfwRuntime()
{
  if (runtimeUsers == 0) {
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
