#pragma once
#include <Jade/Assets/Assets.hpp>
#include <Jade/World/World.hpp>

namespace Jade::Animation {
class System final {
public:
  void update(Jade::World& scene, const Assets& assets, float dt) const;
};
} // namespace Jade::Animation
