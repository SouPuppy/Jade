#pragma once
#include <Jade/Assets/Assets.hpp>
#include <Jade/Core/Scene.hpp>

namespace Jade::Animation {
class System final {
public:
  void update(Scene& scene, const Assets& assets, float dt) const;
};
} // namespace Jade::Animation
