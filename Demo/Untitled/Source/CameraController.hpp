#pragma once
#include <Jade/Jade.hpp>

namespace Untitled {
class CameraController {
public:
  int step = 0;
  void rotate(int delta);
  Jade::Vec3 forward() const;
  Jade::Vec3 right() const;
  void update(Jade::Entity camera, Jade::Vec3 target) const;
};
int relativeDirection(Jade::Vec3 facing, const CameraController& camera);
} // namespace Untitled
