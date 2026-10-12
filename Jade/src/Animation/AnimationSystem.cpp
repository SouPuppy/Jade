#include <Jade/Animation/AnimationSystem.hpp>
#include <Jade/World/Streaming.hpp>
#include <Jade/Animation/Animator.hpp>
#include <Jade/Graphics/SpriteRenderer.hpp>
#include <cmath>
#include <stdexcept>

namespace Jade::Animation {
void System::update(Jade::World& scene, const Assets& assets, float dt) const {
  if (!std::isfinite(dt) || dt < 0) throw std::invalid_argument("Animation dt must be finite and nonnegative.");
  scene.registry().each<Animator, SpriteRenderer>([&](Entity entity, Animator& animator, SpriteRenderer& sprite) {
    if (entity.has<Residency>() && !entity.get<Residency>().simulation) return;
    if (animator.clip.empty()) return;
    const auto& clip = assets.get(animator.clip);
    if (clip.frames.empty()) throw std::invalid_argument("Animation clip has no frames.");
    double duration = 0;
    for (const auto& frame : clip.frames) {
      if (!std::isfinite(frame.duration) || frame.duration <= 0)
        throw std::invalid_argument("Animation frame duration must be positive and finite.");
      duration += frame.duration;
    }
    if (!std::isfinite(animator.time) || animator.time < 0)
      throw std::invalid_argument("Animation time must be finite and nonnegative.");
    const double time = animator.time + (animator.paused ? 0.0 : dt);
    animator.finished = !clip.loop && time >= duration;
    animator.time = clip.loop ? std::fmod(time, duration) : std::min(time, duration);
    double boundary = 0;
    animator.frame = clip.frames.size() - 1;
    for (std::size_t i = 0; i < clip.frames.size(); ++i) {
      boundary += clip.frames[i].duration;
      if (animator.time < boundary) { animator.frame = i; break; }
    }
    sprite.frame = clip.frames[animator.frame];
  });
}
} // namespace Jade::Animation
