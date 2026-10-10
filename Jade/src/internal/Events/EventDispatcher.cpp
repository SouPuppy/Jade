#include "EventDispatcher.hpp"

#include <Jade/Log.hpp>

#include <algorithm>
#include <array>
#include <exception>
#include <utility>

namespace Jade::Detail {
namespace {

const Log::Logger logger{"Events"};

std::string_view KeyName(Key key) noexcept
{
  static constexpr auto names = std::to_array<std::string_view>({
    "Unknown", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "Space", "Up", "Down", "Left", "Right", "Escape", "Enter", "Tab", "Backspace",
    "Delete", "Insert", "Home", "End", "PageUp", "PageDown",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "LeftShift", "RightShift", "LeftControl", "RightControl",
    "LeftAlt", "RightAlt", "LeftSuper", "RightSuper"});
  static_assert(names.size() == static_cast<std::size_t>(Key::Count));
  const auto index = static_cast<std::size_t>(key);
  return index < names.size() ? names[index] : "Unknown";
}

std::string_view ButtonName(MouseButton button) noexcept
{
  static constexpr auto names = std::to_array<std::string_view>({
    "Unknown", "Left", "Right", "Middle", "Button4", "Button5", "Button6", "Button7", "Button8"});
  static_assert(names.size() == static_cast<std::size_t>(MouseButton::Count));
  const auto index = static_cast<std::size_t>(button);
  return index < names.size() ? names[index] : "Unknown";
}

std::string ChordName(KeyChord chord)
{
  std::string result;
  for (const auto [modifier, prefix] : {
    std::pair{Modifiers::Control, "C-"}, {Modifiers::Shift, "S-"},
    {Modifiers::Alt, "A-"}, {Modifiers::Super, "M-"}}) {
    if ((chord.modifiers & modifier) != Modifiers::None) result += prefix;
  }
  result += KeyName(chord.key);
  return result;
}

std::string_view EventName(KeyAction action) noexcept
{
  switch (action) {
  case KeyAction::Pressed: return "KeyPressed";
  case KeyAction::Released: return "KeyReleased";
  case KeyAction::Held: return "KeyHeld";
  }
  return "Unknown";
}

std::string_view EventName(MouseAction action) noexcept
{
  switch (action) {
  case MouseAction::Pressed: return "MousePressed";
  case MouseAction::Released: return "MouseReleased";
  case MouseAction::Held: return "MouseHeld";
  }
  return "Unknown";
}

} // namespace

void EventDispatcher::Prune()
{
  std::vector<std::shared_ptr<Slot>> removed;
  if (Log::Enabled(Log::Level::Debug)) {
    for (const auto& slot : m_Slots) {
      if (!*slot->active) removed.push_back(slot);
    }
  }
  std::erase_if(m_Slots, [](const auto& slot) { return !*slot->active; });
  // Emit after mutation: a logging sink may register or dispatch other events.
  for (const auto& slot : removed) {
    logger << Log::debug("Disconnected: id={} binding={}", slot->id, slot->binding);
  }
}

std::shared_ptr<bool> EventDispatcher::Add(std::shared_ptr<Slot> slot)
{
  slot->id = m_NextId++;
  m_Slots.push_back(slot);
  logger << Log::debug("Subscribed: id={} binding={}", slot->id, slot->binding);
  return slot->active;
}

std::shared_ptr<bool> EventDispatcher::Connect(KeyAction action, KeyChord chord, KeyHandler handler)
{
  Prune();
  auto slot = std::make_shared<Slot>();
  slot->action = action;
  slot->chord = chord;
  slot->keyHandler = std::move(handler);
  slot->binding = std::format("{}<{}>", EventName(action), ChordName(chord));
  return Add(std::move(slot));
}

std::shared_ptr<bool> EventDispatcher::Connect(FrameHandler handler)
{
  Prune();
  auto slot = std::make_shared<Slot>();
  slot->frameHandler = std::move(handler);
  slot->binding = "FrameUpdate";
  return Add(std::move(slot));
}

std::shared_ptr<bool> EventDispatcher::Connect(MouseAction action, MouseButton button, ButtonHandler handler)
{
  Prune();
  auto slot = std::make_shared<Slot>();
  slot->mouseAction = action;
  slot->button = button;
  slot->buttonHandler = std::move(handler);
  slot->binding = std::format("{}<{}>", EventName(action), ButtonName(button));
  return Add(std::move(slot));
}

std::shared_ptr<bool> EventDispatcher::Connect(ScrollHandler handler)
{
  Prune();
  auto slot = std::make_shared<Slot>();
  slot->scrollHandler = std::move(handler);
  slot->binding = "MouseScrolled";
  return Add(std::move(slot));
}

std::shared_ptr<bool> EventDispatcher::Connect(MouseHandler handler)
{
  Prune();
  auto slot = std::make_shared<Slot>();
  slot->mouseHandler = std::move(handler);
  slot->binding = "MouseMoved";
  return Add(std::move(slot));
}

template<typename Matches, typename Invoke>
EventDispatcher::DispatchResult EventDispatcher::DispatchTo(
  std::string_view event, Matches matches, Invoke invoke)
{
  Prune();
  // A snapshot permits registration and cancellation from inside a callback.
  // Shared slots keep mutable callback state and executing handlers alive.
  const auto snapshot = m_Slots;
  DispatchResult result;
  for (const auto& slot : snapshot) {
    if (!*slot->active || !matches(*slot)) continue;
    ++result.handlers;
    try {
      if (invoke(*slot) == EventFlow::Stop) {
        result.flow = EventFlow::Stop;
        result.consumedBy = slot->id;
        break;
      }
    } catch (const std::exception& error) {
      logger << Log::error("Handler failed: event={} subscription={} binding={} reason={}",
        event, slot->id, slot->binding, error.what());
      throw;
    } catch (...) {
      logger << Log::error("Handler failed: event={} subscription={} binding={} reason=non-standard exception",
        event, slot->id, slot->binding);
      throw;
    }
  }
  return result;
}

EventFlow EventDispatcher::Dispatch(KeyAction action, const KeyEvent& event)
{
  const auto result = DispatchTo(EventName(action), [&](const Slot& slot) {
    return slot.keyHandler && slot.action == action
      && slot.chord == KeyChord{event.key, event.modifiers};
  }, [&](const Slot& slot) { return slot.keyHandler(event); });
  try {
    if (action == KeyAction::Held) {
      if (Log::Enabled(Log::Level::Trace)) {
        logger << Log::trace("{}: binding={} dt={:.4f}s handlers={} consumed_by={}",
          EventName(action), ChordName({event.key, event.modifiers}), event.dt,
          result.handlers, result.consumedBy);
      }
    } else if (Log::Enabled(Log::Level::Debug)) {
      logger << Log::debug("{}: binding={} handlers={} consumed_by={}",
        EventName(action), ChordName({event.key, event.modifiers}), result.handlers, result.consumedBy);
    }
  } catch (...) {
    // Formatting a chord for diagnostics cannot fail an otherwise successful dispatch.
  }
  return result.flow;
}

EventFlow EventDispatcher::Dispatch(const FrameUpdate& event)
{
  const auto result = DispatchTo("FrameUpdate",
    [](const Slot& slot) { return static_cast<bool>(slot.frameHandler); },
    [&](const Slot& slot) { return slot.frameHandler(event); });
  logger << Log::trace("FrameUpdate: frame={} dt={:.4f}s handlers={} consumed_by={}",
    event.frame, event.dt, result.handlers, result.consumedBy);
  return result.flow;
}

EventFlow EventDispatcher::Dispatch(MouseAction action, const MouseEvent& event)
{
  const auto result = DispatchTo(EventName(action), [&](const Slot& slot) {
    // A button binding matches regardless of modifiers, including a changed modifier on release.
    return slot.buttonHandler && slot.mouseAction == action && slot.button == event.button;
  }, [&](const Slot& slot) { return slot.buttonHandler(event); });
  if (action == MouseAction::Held) {
    logger << Log::trace("{}: button={} dt={:.4f}s handlers={} consumed_by={}",
      EventName(action), ButtonName(event.button), event.dt, result.handlers, result.consumedBy);
  } else {
    logger << Log::debug("{}: button={} handlers={} consumed_by={}",
      EventName(action), ButtonName(event.button), result.handlers, result.consumedBy);
  }
  return result.flow;
}

EventFlow EventDispatcher::Dispatch(const MouseScrolled& event)
{
  const auto result = DispatchTo("MouseScrolled",
    [](const Slot& slot) { return static_cast<bool>(slot.scrollHandler); },
    [&](const Slot& slot) { return slot.scrollHandler(event); });
  logger << Log::trace("MouseScrolled: dx={:.3f} dy={:.3f} handlers={} consumed_by={}",
    event.dx, event.dy, result.handlers, result.consumedBy);
  return result.flow;
}

EventFlow EventDispatcher::Dispatch(const MouseMoved& event)
{
  const auto result = DispatchTo("MouseMoved",
    [](const Slot& slot) { return static_cast<bool>(slot.mouseHandler); },
    [&](const Slot& slot) { return slot.mouseHandler(event); });
  logger << Log::trace("MouseMoved: x={:.2f} y={:.2f} dx={:.2f} dy={:.2f} handlers={} consumed_by={}",
    event.x, event.y, event.dx, event.dy, result.handlers, result.consumedBy);
  return result.flow;
}

} // namespace Jade::Detail
