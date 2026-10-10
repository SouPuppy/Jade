#pragma once

#include <Jade/Vector.hpp>
#include <Jade/Color.hpp>
#include <Jade/Mesh/Mesh.hpp>

namespace Jade {

// A mesh in the 3D world. Transform order is scale, X/Y/Z rotations, translation.
// Camera derives from Object and defines its own rotation order.
class Object {
public:
  Object() = default;
  virtual ~Object() = default;

  virtual void setPos(Vec3 position);
  void setRotation(Vec3 degrees);
  void setScale(Vec3 scale);
  // Takes an owned snapshot. Invalid meshes leave the previous mesh intact.
  void setMesh(Mesh mesh);
  void setColor(Color color);

  const Vec3& Position() const noexcept { return m_Position; }
  const Vec3& getPos() const noexcept { return m_Position; }
  const Vec3& Rotation() const noexcept { return m_Rotation; }
  const Vec3& Scale() const noexcept { return m_Scale; }
  const Mesh& GetMesh() const noexcept { return m_Mesh; }
  const Color& GetColor() const noexcept { return m_Color; }

private:
  Vec3 m_Position{0, 0, 0};
  Vec3 m_Rotation{0, 0, 0};
  Vec3 m_Scale{1, 1, 1};
  Mesh m_Mesh;
  Color m_Color{1, 1, 1, 1};
};

} // namespace Jade
