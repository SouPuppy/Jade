#pragma once

namespace Jade::Renderer {

struct RenderSettings {
  float resolutionScale = 1;
  float exposureEV = 0;
  bool operator==(const RenderSettings&) const = default;
};

} // namespace Jade::Renderer
