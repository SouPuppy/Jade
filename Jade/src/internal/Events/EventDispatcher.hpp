#pragma once

#include <Jade/Events/Event.hpp>
#include <Jade/Events/FrameUpdate.hpp>
#include <Jade/Events/Keyboard.hpp>
#include <Jade/Events/Mouse.hpp>
#include <Jade/Events/MouseMoved.hpp>
#include <Jade/Events/MouseScrolled.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Jade::Detail {

class EventDispatcher final {
public:
  using KeyHandler = std::function<EventFlow(const KeyEvent&)>;
  using FrameHandler = std::function<EventFlow(const FrameUpdate&)>;
  using MouseHandler = std::function<EventFlow(const MouseMoved&)>;
  using ButtonHandler = std::function<EventFlow(const MouseEvent&)>;
  using ScrollHandler = std::function<EventFlow(const MouseScrolled&)>;

  std::shared_ptr<bool> Connect(KeyAction action, KeyChord chord, KeyHandler handler);
  std::shared_ptr<bool> Connect(MouseAction action, MouseButton button, ButtonHandler handler);
  std::shared_ptr<bool> Connect(FrameHandler handler);
  std::shared_ptr<bool> Connect(MouseHandler handler);
  std::shared_ptr<bool> Connect(ScrollHandler handler);
  EventFlow Dispatch(KeyAction action, const KeyEvent& event);
  EventFlow Dispatch(MouseAction action, const MouseEvent& event);
  EventFlow Dispatch(const FrameUpdate& event);
  EventFlow Dispatch(const MouseMoved& event);
  EventFlow Dispatch(const MouseScrolled& event);

private:
  struct Slot {
    std::size_t id = 0;
    std::string binding;
    std::shared_ptr<bool> active = std::make_shared<bool>(true);
    KeyAction action = KeyAction::Pressed;
    KeyChord chord;
    MouseAction mouseAction = MouseAction::Pressed;
    MouseButton button = MouseButton::Unknown;
    KeyHandler keyHandler;
    FrameHandler frameHandler;
    MouseHandler mouseHandler;
    ButtonHandler buttonHandler;
    ScrollHandler scrollHandler;
  };

  struct DispatchResult {
    EventFlow flow = EventFlow::Continue;
    std::size_t handlers = 0;
    std::size_t consumedBy = 0;
  };

  std::shared_ptr<bool> Add(std::shared_ptr<Slot> slot);
  template<typename Matches, typename Invoke>
  DispatchResult DispatchTo(std::string_view event, Matches matches, Invoke invoke);
  void Prune();
  std::vector<std::shared_ptr<Slot>> m_Slots;
  std::size_t m_NextId = 1;
};

} // namespace Jade::Detail
