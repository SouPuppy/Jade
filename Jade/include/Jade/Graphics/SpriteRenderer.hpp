#pragma once
#include <Jade/Animation/SpriteFrame.hpp>

namespace Jade {
struct SpriteRenderer {
  SpriteFrame frame;
  Vec2 size{1, 1};
  // Local visual offset in world units, independent of the source-frame pivot.
  Vec2 offset{};
  bool billboard = true;
  float alphaCutoff = 0.5f;
};
} // namespace Jade
