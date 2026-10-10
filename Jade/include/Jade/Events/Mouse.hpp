#pragma once

#include <Jade/Input/MouseButton.hpp>

namespace Jade {

enum class MouseAction { Pressed, Released, Held };

struct MouseEvent {
  MouseButton button = MouseButton::Unknown;
  Modifiers modifiers = Modifiers::None;
  // Held: seconds since the previous frame. Pressed/Released: zero.
  float dt = 0;
};

namespace Detail {

template<KeyString Name, MouseAction Action>
struct MouseButtonEvent final : MouseEvent {
  static constexpr MouseButton binding = ParseMouseButton(Name.view());
  static constexpr MouseAction action = Action;
  static_assert(binding != MouseButton::Unknown,
    "Invalid mouse binding. Use Left, Right, Middle or Button4 through Button8.");

  explicit MouseButtonEvent(const MouseEvent& event) : MouseEvent(event) {}
};

} // namespace Detail

template<KeyString Name>
using MousePressed = Detail::MouseButtonEvent<Name, MouseAction::Pressed>;

template<KeyString Name>
using MouseReleased = Detail::MouseButtonEvent<Name, MouseAction::Released>;

template<KeyString Name>
using MouseHeld = Detail::MouseButtonEvent<Name, MouseAction::Held>;

} // namespace Jade
