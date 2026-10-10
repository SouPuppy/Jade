#pragma once
#include <Jade/Renderer/Common/FrameBackend.hpp>
#include <memory>
namespace Jade::Detail {
struct RasterizationAccess;
}
namespace Jade::Renderer {
struct RasterizationConfig : RenderSettings {
  std::uint32_t areaSamples = 16;
  std::uint32_t shadowResolution = 256;
  std::uint32_t reflectionProbeResolution = 128;
  std::uint32_t maxReflectionProbes = 4;
  std::uint32_t reflectionSteps = 64;
  std::uint32_t refractionSteps = 64;
  bool shadows = true;
  bool reflections = true;
  bool refractions = true;
};
class Rasterization final : public FrameBackend {
public:
  explicit Rasterization(RasterizationConfig config = {});
  ~Rasterization();
  Rasterization(const Rasterization &) = delete;
  Rasterization &operator=(const Rasterization &) = delete;
  std::string_view name() const noexcept override { return "Rasterization"; }
  RenderSettings settings() const override;
  void render(const SceneSnapshot &, const FrameRequest &, FrameTarget &) override;

private:
  friend struct Jade::Detail::RasterizationAccess;
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Jade::Renderer
