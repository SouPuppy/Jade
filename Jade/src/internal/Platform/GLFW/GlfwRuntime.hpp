#pragma once

namespace Jade::Detail {

// Shared lifetime for GLFW; all users belong to the main thread.
class GlfwRuntime final {
public:
  GlfwRuntime();
  ~GlfwRuntime();

  GlfwRuntime(const GlfwRuntime&) = delete;
  GlfwRuntime& operator=(const GlfwRuntime&) = delete;
};

} // namespace Jade::Detail
