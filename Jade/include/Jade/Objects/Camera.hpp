#pragma once

#include <Jade/Objects/Object.hpp>

namespace Jade {

enum class Projection {
  Perspective,
  Orthographic
};

// Right-handed world: Y points up. FOV is the vertical angle in degrees.
// Local rotation uses yaw(Y) * pitch(X) * roll(Z), relative to lookAt.
class Camera final : public Object {
public:
  explicit Camera(Projection projection = Projection::Perspective);

  // Translates eye and look-at target together, preserving orientation.
  void setPos(Vec3 position) override;
  Vec3 getFront() const;
  Vec3 getRight() const;
  Vec3 getUp() const;
  // Aims at the target and resets the inherited local rotation offset.
  void lookAt(Vec3 target, Vec3 up = {0, 1, 0});
  // Switches mode while retaining each mode's zoom settings and the clip planes.
  void setProjection(Projection projection);
  void setPerspective(float fovDegrees = 60.0f, float nearPlane = 0.1f, float farPlane = 100.0f);
  void setOrthographic(float viewHeight = 6.0f, float nearPlane = 0.1f, float farPlane = 100.0f);

  Projection Mode() const noexcept { return m_Projection; }
  const Vec3& Target() const noexcept { return m_Target; }
  const Vec3& Up() const noexcept { return m_Up; }
  float FOV() const noexcept { return m_FOV; }
  float ViewHeight() const noexcept { return m_ViewHeight; }
  float NearPlane() const noexcept { return m_NearPlane; }
  float FarPlane() const noexcept { return m_FarPlane; }

private:
  Projection m_Projection;
  Vec3 m_Target{0, 0, 0};
  Vec3 m_Up{0, 1, 0};
  float m_FOV = 60.0f;
  float m_ViewHeight = 6.0f;
  float m_NearPlane = 0.1f;
  float m_FarPlane = 100.0f;
};

} // namespace Jade
