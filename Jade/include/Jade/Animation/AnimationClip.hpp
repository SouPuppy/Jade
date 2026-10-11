#pragma once
#include <Jade/Animation/SpriteFrame.hpp>
#include <vector>

namespace Jade {
struct AnimationClip {
  std::vector<SpriteFrame> frames;
  bool loop = true;
};
using AnimationClipHandle = AssetHandle<AnimationClip>;
} // namespace Jade
