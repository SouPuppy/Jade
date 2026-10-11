#pragma once

#include <Jade/Graphics/CameraControl.hpp>
#include <Jade/Core/Transform.hpp>
#include <Jade/Graphics/Camera.hpp>
#include <Jade/Events/Keyboard.hpp>
#include <Jade/Events/Mouse.hpp>
#include <Jade/Events/MouseMoved.hpp>
#include <array>
#include <optional>

namespace Jade::Detail {

// Shared by selected scene cameras and Runtime's independent observer.
class CameraNavigation {
public:
  void synchronize(Transform&, const CameraControl&, const Camera* lens = nullptr);
  bool key(KeyAction, const KeyEvent&, const CameraControl&);
  bool button(MouseAction, const MouseEvent&, const CameraControl&);
  void mouse(Transform&, const Camera*, const MouseMoved&, const CameraControl&);
  void scroll(Camera&, double);
  void update(Transform&, float dt, const CameraControl&);
  void reset() noexcept;
  void beginFrame() noexcept { m_Held.fill(false); m_Modifiers = Modifiers::None; }
  void suspend(bool value) noexcept { if (m_Suspended != value) { reset(); m_Suspended = value; } }
  bool suspended() const noexcept { return m_Suspended; }
private:
  std::array<bool, static_cast<unsigned>(Key::Count)> m_Held{};
  Modifiers m_Modifiers = Modifiers::None;
  bool m_Looking = false, m_Panning = false;
  bool m_Suspended = false;
  std::optional<Projection> m_Projection;
  std::optional<CameraControl> m_Control;
  std::optional<Quaternion> m_Rotation;
  struct Zoom { Camera reference; double scroll; float applied; };
  std::optional<Zoom> m_PerspectiveZoom, m_OrthographicZoom;
};
} // namespace Jade::Detail
