#pragma once

#include <Jade/Renderer/Common/Image.hpp>
#include <optional>
#include <string>

namespace Jade::Renderer {

struct RenderProgress {
  std::uint64_t revision = 0;
  std::optional<std::uint64_t> samplesPerPixel;
  ImageSize size;
  double sampleMilliseconds = 0;
  bool inFlight = false;
  std::string rendererName;
};

} // namespace Jade::Renderer
