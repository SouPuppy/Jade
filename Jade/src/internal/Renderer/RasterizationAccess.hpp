#pragma once
#include "../../Renderer/Rasterization/RasterPipeline.hpp"
namespace Jade::Detail {
struct RasterizationAccess {
  static RasterStatistics statistics(const Renderer::Rasterization &);
};
} // namespace Jade::Detail
