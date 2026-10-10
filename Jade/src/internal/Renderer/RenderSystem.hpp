#pragma once

#include <Jade/Assets/Assets.hpp>
#include <Jade/Core/Scene.hpp>

#include "Renderer.hpp"

namespace Jade::Detail {

// Stateless bridge from component data to drawing commands.
class RenderSystem final {
public:
  static void Render(const Scene& scene, const Assets& assets,
    Renderer& renderer, FramebufferSize size, bool axisGizmo);
};

} // namespace Jade::Detail
