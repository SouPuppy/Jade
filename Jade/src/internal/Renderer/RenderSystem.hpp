#pragma once

#include <Jade/Assets/Assets.hpp>
#include <Jade/Core/World.hpp>

#include "Renderer.hpp"

namespace Jade::Detail {

// Stateless bridge from component data to drawing commands.
class RenderSystem final {
public:
  static void Render(World& world, const Assets& assets, Entity camera,
    Renderer& renderer, FramebufferSize size, bool axisGizmo);
};

} // namespace Jade::Detail
