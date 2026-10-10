#pragma once

#include <memory>
#include <utility>

namespace Jade {

enum class EventFlow { Continue, Stop };

class Application;

// Ignoring or destroying the handle keeps the subscription registered.
// disconnect() cancels it; destruction of Application expires all handles.
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
  explicit EventConnection(std::weak_ptr<bool> active) : m_Active(std::move(active)) {}
  std::weak_ptr<bool> m_Active;
};

} // namespace Jade
