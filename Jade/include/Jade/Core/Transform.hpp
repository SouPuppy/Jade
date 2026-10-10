#pragma once

#include <Jade/Math/Quaternion.hpp>
#include <Jade/Math/Vector.hpp>

namespace Jade {

struct Transform {
  Vec3 position;
  Quaternion rotation;
  Vec3 scale{1, 1, 1};
};

} // namespace Jade
