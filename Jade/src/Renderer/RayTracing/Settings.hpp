#pragma once
#include <Jade/Renderer/RayTracing/RayTracingSettings.hpp>
#include <cmath>
#include <stdexcept>

namespace Jade::Detail {
inline void validateRayTracingSettings(const Renderer::RayTracingSettings& settings)
{
  using namespace Renderer;
  if (settings.device != RayDevice::CPU && settings.device != RayDevice::GPU)
    throw std::invalid_argument("Unknown ray device.");
  if (settings.mode != RayMode::RayCast && settings.mode != RayMode::PathTrace)
    throw std::invalid_argument("Unknown ray mode.");
  if (settings.output != RayOutput::Color && settings.output != RayOutput::Normal)
    throw std::invalid_argument("Unknown ray output.");
  if (!std::isfinite(settings.resolutionScale) || settings.resolutionScale <= 0 || settings.resolutionScale > 8 ||
      !std::isfinite(settings.exposureEV) || std::abs(settings.exposureEV) > 64)
    throw std::invalid_argument("Invalid ray resolution scale or exposure.");
  if (settings.maxBounces == 0 || settings.maxBounces > 1024 || settings.rrStartBounce == 0)
    throw std::invalid_argument("Path depth must be within [1, 1024] and roulette start positive.");
  if (settings.minSamplesPerFrame == 0 || settings.minSamplesPerFrame > 1024)
    throw std::invalid_argument("Frame sample minimum must be within [1, 1024].");
}
} // namespace Jade::Detail
