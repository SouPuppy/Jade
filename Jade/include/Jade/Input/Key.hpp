#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Jade {

// Jade key values are independent of the window backend.
enum class Key : std::uint16_t {
  Unknown,
  A, B, C, D, E, F, G, H, I, J, K, L, M,
  N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,
  Space, Up, Down, Left, Right,
  Escape, Enter, Tab, Backspace, Delete, Insert,
  Home, End, PageUp, PageDown,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  LeftShift, RightShift, LeftControl, RightControl,
  LeftAlt, RightAlt, LeftSuper, RightSuper,
  Count
};

enum class Modifiers : std::uint8_t {
  None = 0, Shift = 1, Control = 2, Alt = 4, Super = 8
};

constexpr Modifiers operator|(Modifiers a, Modifiers b) noexcept
{
  return static_cast<Modifiers>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr Modifiers operator&(Modifiers a, Modifiers b) noexcept
{
  return static_cast<Modifiers>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
}

struct KeyChord {
  Key key = Key::Unknown;
  Modifiers modifiers = Modifiers::None;
  constexpr bool operator==(const KeyChord&) const = default;
};

// Structural value type allows KeyHeld<"C-w"> with standard C++20.
template<std::size_t N>
struct KeyString {
  char value[N]{};

  constexpr KeyString(const char (&text)[N]) noexcept
  {
    for (std::size_t index = 0; index < N; ++index) {
      value[index] = text[index];
    }
  }

  constexpr std::string_view view() const noexcept { return {value, N - 1}; }
};

namespace Detail {

constexpr char Lower(char value) noexcept
{
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

constexpr bool KeyNameEquals(std::string_view a, std::string_view b) noexcept
{
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t index = 0; index < a.size(); ++index) {
    if (Lower(a[index]) != Lower(b[index])) {
      return false;
    }
  }
  return true;
}

constexpr KeyChord ParseKeyChord(std::string_view text) noexcept
{
  KeyChord result;
  while (text.size() >= 2 && text[1] == '-') {
    Modifiers modifier;
    switch (Lower(text[0])) {
      case 'c': modifier = Modifiers::Control; break;
      case 's': modifier = Modifiers::Shift; break;
      case 'a': modifier = Modifiers::Alt; break;
      case 'm': modifier = Modifiers::Super; break;
      default: return {};
    }
    if ((result.modifiers & modifier) != Modifiers::None) {
      return {};
    }
    result.modifiers = result.modifiers | modifier;
    text.remove_prefix(2);
  }
  if (text.size() == 1) {
    const char letter = Lower(text[0]);
    if (letter >= 'a' && letter <= 'z') {
      result.key = static_cast<Key>(static_cast<unsigned>(Key::A) + letter - 'a');
    } else if (letter >= '0' && letter <= '9') {
      result.key = static_cast<Key>(static_cast<unsigned>(Key::D0) + letter - '0');
    }
    return result;
  }
  constexpr struct { std::string_view name; Key key; } names[] = {
    {"Space", Key::Space}, {"Up", Key::Up}, {"Down", Key::Down},
    {"Left", Key::Left}, {"Right", Key::Right}, {"Escape", Key::Escape},
    {"Enter", Key::Enter}, {"Tab", Key::Tab}, {"Backspace", Key::Backspace},
    {"Delete", Key::Delete}, {"Insert", Key::Insert}, {"Home", Key::Home},
    {"End", Key::End}, {"PageUp", Key::PageUp}, {"PageDown", Key::PageDown},
    {"LeftShift", Key::LeftShift}, {"RightShift", Key::RightShift},
    {"LeftControl", Key::LeftControl}, {"RightControl", Key::RightControl},
    {"LeftAlt", Key::LeftAlt}, {"RightAlt", Key::RightAlt},
    {"LeftSuper", Key::LeftSuper}, {"RightSuper", Key::RightSuper}
  };
  for (const auto& entry : names) {
    if (KeyNameEquals(text, entry.name)) {
      result.key = entry.key;
      return result;
    }
  }
  if (text.size() >= 2 && text.size() <= 3 && Lower(text[0]) == 'f') {
    unsigned number = 0;
    for (std::size_t index = 1; index < text.size(); ++index) {
      if (text[index] < '0' || text[index] > '9') {
        return {};
      }
      number = number * 10 + text[index] - '0';
    }
    if (number >= 1 && number <= 12 && text[1] != '0') {
      result.key = static_cast<Key>(static_cast<unsigned>(Key::F1) + number - 1);
    }
  }
  return result;
}

} // namespace Detail
} // namespace Jade
