#pragma once

#include <Jade/Input/Key.hpp>

namespace Jade {

enum class KeyAction { Pressed, Released, Held };

struct KeyEvent {
  Key key = Key::Unknown;
  Modifiers modifiers = Modifiers::None;
  // Held: seconds since the previous frame. Pressed/Released: zero.
  float dt = 0;
};

namespace Detail {

template<KeyString Name, KeyAction Action>
struct KeyboardEvent final : KeyEvent {
  static constexpr KeyChord chord = ParseKeyChord(Name.view());
  static constexpr KeyAction action = Action;
  static_assert(chord.key != Key::Unknown,
    "Invalid key binding. Use w, Space, Up/Down/Left/Right or prefixes C-/S-/A-/M-.");

  explicit KeyboardEvent(const KeyEvent& event) : KeyEvent(event) {}
};

} // namespace Detail

template<KeyString Name>
using KeyPressed = Detail::KeyboardEvent<Name, KeyAction::Pressed>;

template<KeyString Name>
using KeyReleased = Detail::KeyboardEvent<Name, KeyAction::Released>;

template<KeyString Name>
using KeyHeld = Detail::KeyboardEvent<Name, KeyAction::Held>;

} // namespace Jade
