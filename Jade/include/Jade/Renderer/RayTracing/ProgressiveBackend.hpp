#pragma once
#include <Jade/Renderer/Common/Backend.hpp>
#include <Jade/Renderer/RayTracing/RayScene.hpp>
#include <stop_token>
namespace Jade::Renderer {
struct ProgressiveSettings : RenderSettings {
  std::uint64_t maxSamples = 0;
  std::uint64_t seed = 0;
};
struct RenderRequest {
  CameraFrame camera;
  ImageSize size;
  std::uint64_t sampleIndex = 0;
  std::uint64_t seed = 0;
  std::stop_token cancellation;
};
class ProgressiveBackend : public Backend {
public:
  std::string_view name() const noexcept override { return "Progressive"; }
  Execution execution() const noexcept final { return Execution::Progressive; }
  RenderSettings settings() const final { return samplingSettings(); }
  virtual ProgressiveSettings samplingSettings() const { return {}; }
  virtual void render(const RayScene &, const RenderRequest &, Film &) = 0;
};
} // namespace Jade::Renderer
