#pragma once

#include <Jade/WindowConfig.hpp>

struct GLFWwindow;

namespace Jade::Detail {

// Backend creation policy stays private. Vulkan will use None.
enum class WindowClientApi {
  OpenGL,
  None
};

class GlfwWindow final {
public:
  GlfwWindow(const WindowConfig& config, WindowClientApi clientApi);
  ~GlfwWindow();

  GlfwWindow(const GlfwWindow&) = delete;
  GlfwWindow& operator=(const GlfwWindow&) = delete;

  GLFWwindow* Handle() const { return m_Handle; }

private:
  GLFWwindow* m_Handle = nullptr;
};

} // namespace Jade::Detail
