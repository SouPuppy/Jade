#pragma once
#include <Jade/Renderer/RayTracing/ProgressiveBackend.hpp>

namespace Jade::Renderer {
enum class RayMode { RayCast, PathTrace };
enum class RayDevice { CPU, GPU };
// Color evaluates the selected algorithm; Normal displays primary-hit world normals.
// BRDF/BSDF properties belong to Material, rather than this output selection.
enum class RayOutput { Color, Normal };

struct RayTracingSettings : ProgressiveSettings {
  bool enabled = true;
  RayDevice device = RayDevice::CPU;
  RayMode mode = RayMode::PathTrace;
  RayOutput output = RayOutput::Color;
  bool jitter = true;
  // Transport controls used by PathTrace/Color only.
  std::uint32_t maxBounces = 12;
  std::uint32_t rrStartBounce = 4;
  bool operator==(const RayTracingSettings&) const = default;
};
} // namespace Jade::Renderer
