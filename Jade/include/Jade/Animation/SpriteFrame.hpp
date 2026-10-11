#pragma once
#include <Jade/Graphics/Texture.hpp>

namespace Jade {
struct SpriteRegion {
  float x = 0, y = 0, width = 1, height = 1;
  bool operator==(const SpriteRegion&) const = default;
};
struct SpriteFrame {
  TextureHandle texture;
  SpriteRegion region;
  float duration = 1.0f / 12;
  // Normalized anchor from the bottom-left of the untrimmed canvas.
  Vec2 pivot{0.5f, 0};
};
} // namespace Jade
