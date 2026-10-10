#include "EventDispatcher.hpp"

#include <algorithm>
#include <utility>

namespace Jade::Detail {

void EventDispatcher::Prune()
{
  std::erase_if(m_Slots, [](const auto& slot) { return !*slot->active; });
}

std::shared_ptr<bool> EventDispatcher::Connect(KeyAction action, KeyChord chord, KeyHandler handler)
{
  Prune();
  auto slot = std::make_shared<Slot>();
  slot->action = action;
  slot->chord = chord;
  slot->keyHandler = std::move(handler);
  m_Slots.push_back(slot);
  return slot->active;
}

std::shared_ptr<bool> EventDispatcher::Connect(FrameHandler handler)
{
  Prune();
  auto slot = std::make_shared<Slot>();
  slot->frameHandler = std::move(handler);
  m_Slots.push_back(slot);
  return slot->active;
}

std::shared_ptr<bool> EventDispatcher::Connect(MouseAction action, MouseButton button, ButtonHandler handler)
{
  Prune();
  auto slot = std::make_shared<Slot>();
  slot->mouseAction = action;
  slot->button = button;
  slot->buttonHandler = std::move(handler);
  m_Slots.push_back(slot);
  return slot->active;
}

std::shared_ptr<bool> EventDispatcher::Connect(ScrollHandler handler)
{
  Prune();
  auto slot = std::make_shared<Slot>();
  slot->scrollHandler = std::move(handler);
  m_Slots.push_back(slot);
  return slot->active;
}

std::shared_ptr<bool> EventDispatcher::Connect(MouseHandler handler)
{
  Prune();
  auto slot = std::make_shared<Slot>();
  slot->mouseHandler = std::move(handler);
  m_Slots.push_back(slot);
  return slot->active;
}

EventFlow EventDispatcher::Dispatch(KeyAction action, const KeyEvent& event)
{
  Prune();
  // A snapshot permits registration and cancellation from inside a callback.
  // Shared slots keep mutable callback state and executing handlers alive.
  const auto snapshot = m_Slots;
  for (const auto& slot : snapshot) {
    if (*slot->active && slot->keyHandler && slot->action == action
        && slot->chord == KeyChord{event.key, event.modifiers}) {
      if (slot->keyHandler(event) == EventFlow::Stop) {
        return EventFlow::Stop;
      }
    }
  }
  return EventFlow::Continue;
}

EventFlow EventDispatcher::Dispatch(const FrameUpdate& event)
{
  Prune();
  const auto snapshot = m_Slots;
  for (const auto& slot : snapshot) {
    if (*slot->active && slot->frameHandler
        && slot->frameHandler(event) == EventFlow::Stop) {
      return EventFlow::Stop;
    }
  }
  return EventFlow::Continue;
}

EventFlow EventDispatcher::Dispatch(MouseAction action, const MouseEvent& event)
{
  Prune();
  const auto snapshot = m_Slots;
  for (const auto& slot : snapshot) {
    // A button binding matches regardless of modifiers, including a changed modifier on release.
    if (*slot->active && slot->buttonHandler && slot->mouseAction == action
        && slot->button == event.button && slot->buttonHandler(event) == EventFlow::Stop) {
      return EventFlow::Stop;
    }
  }
  return EventFlow::Continue;
}

EventFlow EventDispatcher::Dispatch(const MouseScrolled& event)
{
  Prune();
  const auto snapshot = m_Slots;
  for (const auto& slot : snapshot) {
    if (*slot->active && slot->scrollHandler
        && slot->scrollHandler(event) == EventFlow::Stop) {
      return EventFlow::Stop;
    }
  }
  return EventFlow::Continue;
}

EventFlow EventDispatcher::Dispatch(const MouseMoved& event)
{
  Prune();
  const auto snapshot = m_Slots;
  for (const auto& slot : snapshot) {
    if (*slot->active && slot->mouseHandler
        && slot->mouseHandler(event) == EventFlow::Stop) {
      return EventFlow::Stop;
    }
  }
  return EventFlow::Continue;
}

} // namespace Jade::Detail
