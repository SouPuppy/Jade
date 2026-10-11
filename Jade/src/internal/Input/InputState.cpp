#include "InputState.hpp"

#include <utility>
#include <cmath>
#include <algorithm>

namespace Jade::Detail {

bool InputState::IsDown(Key key) const noexcept
{
  const auto index = static_cast<std::size_t>(key);
  return index > 0 && index < keyCount && m_Down[index];
}

void InputState::OnKey(Key key, KeyAction action, Modifiers modifiers)
{
  const auto index = static_cast<std::size_t>(key);
  if (index == 0 || index >= keyCount || action == KeyAction::Held) {
    return;
  }
  const bool down = action == KeyAction::Pressed;
  if ((down && !m_Focused) || m_Down[index] == down) {
    return;
  }
  m_Down[index] = down;
  // GLFW also supplies modifiers already held when this window gains focus.
  m_Modifiers = modifiers;
  m_Events.emplace_back(KeyTransition{action, {key, modifiers, 0}});
}

bool InputState::IsDown(MouseButton button) const noexcept
{
  const auto index = static_cast<std::size_t>(button);
  return index > 0 && index < buttonCount && m_Buttons[index];
}

void InputState::OnMouseButton(MouseButton button, MouseAction action, Modifiers modifiers)
{
  const auto index = static_cast<std::size_t>(button);
  if (index == 0 || index >= buttonCount || action == MouseAction::Held) {
    return;
  }
  const bool down = action == MouseAction::Pressed;
  if ((down && !m_Focused) || m_Buttons[index] == down) {
    return;
  }
  m_Buttons[index] = down;
  m_Modifiers = modifiers;
  m_Events.emplace_back(MouseTransition{action, {button, modifiers, 0}});
}

void InputState::OnFocus(bool focused)
{
  m_Focused = focused;
  m_CursorInitialized = false;
  if (!focused) {
    const auto modifiers = CurrentModifiers();
    for (std::size_t index = 1; index < keyCount; ++index) {
      if (m_Down[index]) {
        m_Events.emplace_back(KeyTransition{KeyAction::Released, {static_cast<Key>(index), modifiers, 0}});
      }
    }
    for (std::size_t index = 1; index < buttonCount; ++index) {
      if (m_Buttons[index]) {
        m_Events.emplace_back(MouseTransition{MouseAction::Released,
          {static_cast<MouseButton>(index), modifiers, 0}});
      }
    }
    m_Down.fill(false);
    m_Buttons.fill(false);
    m_Modifiers = Modifiers::None;
  }
}

void InputState::OnCursor(double x, double y, int windowHeight)
{
  if (!m_Focused || !std::isfinite(x) || !std::isfinite(y)) {
    return;
  }
  const double dx = m_CursorInitialized ? x - m_CursorX : 0;
  const double dy = m_CursorInitialized ? y - m_CursorY : 0;
  if (!std::isfinite(dx) || !std::isfinite(dy)) {
    return;
  }
  m_Events.emplace_back(MouseMoved{x, y, dx, dy, windowHeight});
  m_CursorX = x;
  m_CursorY = y;
  m_CursorInitialized = true;
}

void InputState::ResetCursor() noexcept
{
  m_CursorInitialized = false;
  std::erase_if(m_Events, [](const auto &event) { return std::holds_alternative<MouseMoved>(event); });
}

std::vector<InputEvent> InputState::TakeEvents()
{
  std::vector<InputEvent> result;
  result.swap(m_Events);
  return result;
}

void InputState::OnScroll(double dx, double dy)
{
  if (m_Focused && std::isfinite(dx) && std::isfinite(dy) && (dx != 0 || dy != 0)) {
    m_Events.emplace_back(MouseScrolled{dx, dy});
  }
}

void InputState::CaptureError(std::exception_ptr error) noexcept
{
  if (!m_Error) {
    m_Error = std::move(error);
  }
}

void InputState::RethrowError()
{
  if (m_Error) {
    auto error = std::exchange(m_Error, {});
    std::rethrow_exception(error);
  }
}

} // namespace Jade::Detail
