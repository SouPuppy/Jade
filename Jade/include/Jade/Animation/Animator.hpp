#pragma once
#include <Jade/Animation/AnimationClip.hpp>
#include <cstddef>

namespace Jade {
struct Animator {
  AnimationClipHandle clip;
  double time = 0;
  std::size_t frame = 0;
  bool paused = false;
  bool finished = false;
  void play(AnimationClipHandle next, bool preserveTime = false) {
    if (clip == next) return;
    clip = next;
    if (!preserveTime) time = 0;
    frame = 0;
    finished = false;
  }
};
} // namespace Jade
