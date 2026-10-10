#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace Jade {

enum class EventFlow { Continue, Stop };

class Application;
class Scene;

// Ignoring or destroying the handle keeps the subscription registered.
// disconnect() cancels it; destruction of its Application or Scene expires it.
class EventConnection final {
public:
  EventConnection() = default;

  bool connected() const noexcept
  {
    const auto active = m_Active.lock();
    return active && *active;
  }

  void disconnect() const noexcept
  {
    if (const auto active = m_Active.lock()) {
      *active = false;
    }
  }

private:
  friend class Application;
  friend class Scene;
  explicit EventConnection(std::weak_ptr<bool> active) : m_Active(std::move(active)) {}
  std::weak_ptr<bool> m_Active;
};

namespace Detail {

template<class Callback, class Event>
EventFlow InvokeEventCallback(Callback& callback, const Event& event)
{
  if constexpr (std::is_invocable_v<Callback&, const Event&>) {
    using Result = std::invoke_result_t<Callback&, const Event&>;
    static_assert(std::is_same_v<Result, void> || std::is_same_v<Result, EventFlow>,
      "Event callbacks return void or Jade::EventFlow.");
    if constexpr (std::is_same_v<Result, EventFlow>) {
      return std::invoke(callback, event);
    } else {
      std::invoke(callback, event);
      return EventFlow::Continue;
    }
  } else {
    static_assert(std::is_invocable_v<Callback&>,
      "Event callbacks accept either no arguments or const Event&.");
    using Result = std::invoke_result_t<Callback&>;
    static_assert(std::is_same_v<Result, void> || std::is_same_v<Result, EventFlow>,
      "Event callbacks return void or Jade::EventFlow.");
    if constexpr (std::is_same_v<Result, EventFlow>) {
      return std::invoke(callback);
    } else {
      std::invoke(callback);
      return EventFlow::Continue;
    }
  }
}

} // namespace Detail

} // namespace Jade
