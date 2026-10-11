#include "CameraNavigation.hpp"
#include <Jade/Graphics/CameraMotion.hpp>
#include <Jade/Graphics/Motion.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Jade::Detail {
void CameraNavigation::reset() noexcept
{
  m_Held.fill(false);
  m_Modifiers = Modifiers::None;
  m_Looking = m_Panning = false;
  m_Projection.reset();
  m_Rotation.reset();
  m_PerspectiveZoom.reset();
  m_OrthographicZoom.reset();
}

void CameraNavigation::synchronize(Transform& pose, const CameraControl& control, const Camera* lens)
{
  if (control.viewMode != ViewMode::Editor && control.viewMode != ViewMode::FirstPerson)
    throw std::invalid_argument("Unknown camera view mode.");
  for (const auto& keys : control.bindings.movement)
    for (const auto key : keys)
      if (static_cast<unsigned>(key) >= static_cast<unsigned>(Key::Count))
        throw std::invalid_argument("Unknown camera movement key.");
  for (const auto button : {control.bindings.look, control.bindings.pan})
    if (static_cast<unsigned>(button) >= static_cast<unsigned>(MouseButton::Count))
      throw std::invalid_argument("Unknown camera mouse button.");
  if (control.bindings.look != MouseButton::Unknown && control.bindings.look == control.bindings.pan)
    throw std::invalid_argument("Camera look and pan need distinct buttons.");
  if (!std::isfinite(control.speed) || control.speed < 0 ||
      !std::isfinite(control.slowSpeed) || control.slowSpeed < 0 ||
      !std::isfinite(control.sensitivity) || control.sensitivity < 0 ||
      !std::isfinite(control.panDistance) || control.panDistance <= 0)
    throw std::invalid_argument("Camera control speeds/sensitivity must be nonnegative and pan depth positive.");
  if (!m_Control || control != *m_Control) {
    reset();
    m_Control = control;
  }
  if (lens) {
    if (m_Projection && *m_Projection != lens->projection) {
      m_Looking = m_Panning = false;
    }
    m_Projection = lens->projection;
  }
  if (!m_Rotation || pose.rotation != *m_Rotation) {
    CameraMotion::constrain(pose);
    m_Rotation = pose.rotation;
  }
}

bool CameraNavigation::key(KeyAction action, const KeyEvent& event, const CameraControl& control)
{
  bool bound = false;
  for (const auto& keys : control.bindings.movement)
    for (const auto key : keys) bound |= key != Key::Unknown && key == event.key;
  if (bound && action == KeyAction::Held) {
    // Alt/Super chords remain shortcuts; Shift and Control are independent controls.
    if ((event.modifiers & (Modifiers::Alt | Modifiers::Super)) == Modifiers::None)
      m_Held[static_cast<unsigned>(event.key)] = true;
    m_Modifiers = event.modifiers;
  }
  return bound;
}

bool CameraNavigation::button(MouseAction action, const MouseEvent& event, const CameraControl& control)
{
  if (event.button == MouseButton::Unknown) return false;
  const bool look = event.button == control.bindings.look;
  const bool pan = event.button == control.bindings.pan;
  if (action != MouseAction::Held) {
    if (look) m_Looking = action == MouseAction::Pressed;
    if (pan) m_Panning = action == MouseAction::Pressed;
  }
  return look || pan;
}

void CameraNavigation::mouse(Transform& pose, const Camera* lens, const MouseMoved& event,
                             const CameraControl& control)
{
  if (m_Panning && control.viewMode == ViewMode::Editor && lens) {
    CameraMotion::pan(pose, *lens, event.dx, event.dy, event.windowHeight, control.panDistance);
  } else if ((control.viewMode == ViewMode::FirstPerson || m_Looking) && (event.dx != 0 || event.dy != 0)) {
    CameraMotion::look(pose, event.dx * control.sensitivity, -event.dy * control.sensitivity);
    m_Rotation = pose.rotation;
  }
}

void CameraNavigation::update(Transform& pose, float dt, const CameraControl& control)
{
  const auto active = [&](CameraAction action) {
    for (const auto key : control.bindings.movement[static_cast<unsigned>(action)])
      if (key != Key::Unknown && m_Held[static_cast<unsigned>(key)]) return 1.0f;
    return 0.0f;
  };
  const Vec3 direction{active(CameraAction::Right) - active(CameraAction::Left),
    active(CameraAction::Ascend) - active(CameraAction::Descend),
    active(CameraAction::Forward) - active(CameraAction::Backward)};
  const float speed = control.slowModifier != Modifiers::None &&
    (m_Modifiers & control.slowModifier) == control.slowModifier ? control.slowSpeed : control.speed;
  m_Held.fill(false); // Only this frame's unconsumed held events contribute.
  CameraMotion::move(pose, direction, speed * dt);
}

void CameraNavigation::scroll(Camera& camera, double scroll)
{
  if (!std::isfinite(scroll)) throw std::invalid_argument("Camera scroll must be finite.");
  auto& state = camera.projection == Projection::Perspective ? m_PerspectiveZoom : m_OrthographicZoom;
  const float current = camera.projection == Projection::Perspective ? camera.fovDegrees : camera.viewHeight;
  if (!state || state->applied != current) state = Zoom{camera, 0, current};
  auto next = *state;
  next.scroll += std::clamp(scroll, -100.0, 100.0);
  auto lens = camera;
  if (camera.projection == Projection::Perspective) lens.fovDegrees = next.reference.fovDegrees;
  else lens.viewHeight = next.reference.viewHeight;
  Motion::zoom(lens, next.scroll);
  if (camera.projection == Projection::Perspective) camera.fovDegrees = lens.fovDegrees;
  else camera.viewHeight = lens.viewHeight;
  next.applied = camera.projection == Projection::Perspective ? camera.fovDegrees : camera.viewHeight;
  state = next;
}
} // namespace Jade::Detail
