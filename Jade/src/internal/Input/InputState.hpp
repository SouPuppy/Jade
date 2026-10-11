#pragma once

#include <Jade/Events/Keyboard.hpp>
#include <Jade/Events/Mouse.hpp>
#include <Jade/Events/MouseMoved.hpp>
#include <Jade/Events/MouseScrolled.hpp>

#include <array>
#include <exception>
#include <variant>
#include <vector>

namespace Jade::Detail {

struct KeyTransition {
  KeyAction action;
  KeyEvent event;
};

struct MouseTransition {
  MouseAction action;
  MouseEvent event;
};

using InputEvent = std::variant<KeyTransition, MouseTransition, MouseMoved, MouseScrolled>;

class InputState final {
public:
  static constexpr std::size_t keyCount = static_cast<std::size_t>(Key::Count);
  static constexpr std::size_t buttonCount = static_cast<std::size_t>(MouseButton::Count);

  void OnKey(Key key, KeyAction action, Modifiers modifiers);
  void OnMouseButton(MouseButton button, MouseAction action, Modifiers modifiers);
  void OnFocus(bool focused);
  void OnCursor(double x, double y, int windowHeight = 0);
  void ResetCursor() noexcept;
  void OnScroll(double dx, double dy);
  bool IsDown(Key key) const noexcept;
  bool IsDown(MouseButton button) const noexcept;
  Modifiers CurrentModifiers() const noexcept { return m_Modifiers; }
  std::vector<InputEvent> TakeEvents();
  void CaptureError(std::exception_ptr error) noexcept;
  void RethrowError();

private:
  std::array<bool, keyCount> m_Down{};
  std::array<bool, buttonCount> m_Buttons{};
  std::vector<InputEvent> m_Events;
  bool m_Focused = true;
  Modifiers m_Modifiers = Modifiers::None;
  bool m_CursorInitialized = false;
  double m_CursorX = 0;
  double m_CursorY = 0;
  std::exception_ptr m_Error;
};

} // namespace Jade::Detail
