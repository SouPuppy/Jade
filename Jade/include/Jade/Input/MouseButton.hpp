#pragma once

#include <Jade/Input/Key.hpp>

namespace Jade {

enum class MouseButton : std::uint8_t {
  Unknown, Left, Right, Middle, Button4, Button5, Button6, Button7, Button8, Count
};

namespace Detail {

constexpr MouseButton ParseMouseButton(std::string_view name) noexcept
{
  constexpr struct { std::string_view name; MouseButton button; } buttons[] = {
    {"Left", MouseButton::Left}, {"Right", MouseButton::Right},
    {"Middle", MouseButton::Middle}, {"Button4", MouseButton::Button4},
    {"Button5", MouseButton::Button5}, {"Button6", MouseButton::Button6},
    {"Button7", MouseButton::Button7}, {"Button8", MouseButton::Button8}
  };
  for (const auto& entry : buttons) {
    if (KeyNameEquals(name, entry.name)) {
      return entry.button;
    }
  }
  return MouseButton::Unknown;
}

} // namespace Detail
} // namespace Jade
