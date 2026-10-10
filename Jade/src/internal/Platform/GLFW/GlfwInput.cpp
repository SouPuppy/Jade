#include "GlfwInput.hpp"

#include "../../Input/InputState.hpp"

#include <GLFW/glfw3.h>

#include <exception>

namespace Jade::Detail {
namespace {

Key TranslateKey(int key) noexcept
{
  if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
    return static_cast<Key>(static_cast<int>(Key::A) + key - GLFW_KEY_A);
  }
  if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
    return static_cast<Key>(static_cast<int>(Key::D0) + key - GLFW_KEY_0);
  }
  if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
    return static_cast<Key>(static_cast<int>(Key::F1) + key - GLFW_KEY_F1);
  }
  switch (key) {
    case GLFW_KEY_SPACE: return Key::Space;
    case GLFW_KEY_UP: return Key::Up;
    case GLFW_KEY_DOWN: return Key::Down;
    case GLFW_KEY_LEFT: return Key::Left;
    case GLFW_KEY_RIGHT: return Key::Right;
    case GLFW_KEY_ESCAPE: return Key::Escape;
    case GLFW_KEY_ENTER: return Key::Enter;
    case GLFW_KEY_TAB: return Key::Tab;
    case GLFW_KEY_BACKSPACE: return Key::Backspace;
    case GLFW_KEY_DELETE: return Key::Delete;
    case GLFW_KEY_INSERT: return Key::Insert;
    case GLFW_KEY_HOME: return Key::Home;
    case GLFW_KEY_END: return Key::End;
    case GLFW_KEY_PAGE_UP: return Key::PageUp;
    case GLFW_KEY_PAGE_DOWN: return Key::PageDown;
    case GLFW_KEY_LEFT_SHIFT: return Key::LeftShift;
    case GLFW_KEY_RIGHT_SHIFT: return Key::RightShift;
    case GLFW_KEY_LEFT_CONTROL: return Key::LeftControl;
    case GLFW_KEY_RIGHT_CONTROL: return Key::RightControl;
    case GLFW_KEY_LEFT_ALT: return Key::LeftAlt;
    case GLFW_KEY_RIGHT_ALT: return Key::RightAlt;
    case GLFW_KEY_LEFT_SUPER: return Key::LeftSuper;
    case GLFW_KEY_RIGHT_SUPER: return Key::RightSuper;
    default: return Key::Unknown;
  }
}

Modifiers TranslateModifiers(int modifiers) noexcept
{
  Modifiers result = Modifiers::None;
  if ((modifiers & GLFW_MOD_SHIFT) != 0) { result = result | Modifiers::Shift; }
  if ((modifiers & GLFW_MOD_CONTROL) != 0) { result = result | Modifiers::Control; }
  if ((modifiers & GLFW_MOD_ALT) != 0) { result = result | Modifiers::Alt; }
  if ((modifiers & GLFW_MOD_SUPER) != 0) { result = result | Modifiers::Super; }
  return result;
}

MouseButton TranslateMouseButton(int button) noexcept
{
  if (button >= GLFW_MOUSE_BUTTON_1 && button <= GLFW_MOUSE_BUTTON_8) {
    return static_cast<MouseButton>(static_cast<int>(MouseButton::Left) + button - GLFW_MOUSE_BUTTON_1);
  }
  return MouseButton::Unknown;
}

} // namespace

void InstallInputCallbacks(GLFWwindow* window, InputState& input)
{
  input.OnFocus(glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE);
  glfwSetWindowUserPointer(window, &input);
  glfwSetKeyCallback(window, [](GLFWwindow* source, int key, int, int action, int modifiers) noexcept {
    auto& state = *static_cast<InputState*>(glfwGetWindowUserPointer(source));
    if (action == GLFW_REPEAT) {
      return;
    }
    try {
      state.OnKey(TranslateKey(key), action == GLFW_PRESS ? KeyAction::Pressed : KeyAction::Released,
        TranslateModifiers(modifiers));
    } catch (...) {
      state.CaptureError(std::current_exception());
    }
  });
  glfwSetWindowFocusCallback(window, [](GLFWwindow* source, int focused) noexcept {
    auto& state = *static_cast<InputState*>(glfwGetWindowUserPointer(source));
    try {
      state.OnFocus(focused == GLFW_TRUE);
    } catch (...) {
      state.CaptureError(std::current_exception());
    }
  });
  glfwSetCursorPosCallback(window, [](GLFWwindow* source, double x, double y) noexcept {
    auto& state = *static_cast<InputState*>(glfwGetWindowUserPointer(source));
    try {
      state.OnCursor(x, y);
    } catch (...) {
      state.CaptureError(std::current_exception());
    }
  });
  glfwSetMouseButtonCallback(window, [](GLFWwindow* source, int button, int action, int modifiers) noexcept {
    auto& state = *static_cast<InputState*>(glfwGetWindowUserPointer(source));
    if (action != GLFW_PRESS && action != GLFW_RELEASE) {
      return;
    }
    try {
      state.OnMouseButton(TranslateMouseButton(button),
        action == GLFW_PRESS ? MouseAction::Pressed : MouseAction::Released,
        TranslateModifiers(modifiers));
    } catch (...) {
      state.CaptureError(std::current_exception());
    }
  });
  glfwSetScrollCallback(window, [](GLFWwindow* source, double dx, double dy) noexcept {
    auto& state = *static_cast<InputState*>(glfwGetWindowUserPointer(source));
    try {
      state.OnScroll(dx, dy);
    } catch (...) {
      state.CaptureError(std::current_exception());
    }
  });
}

} // namespace Jade::Detail
