#pragma once
#include <Jade/Renderer/Common/RenderSettings.hpp>
#include <Jade/Renderer/Common/RenderProgress.hpp>
#include <Jade/Renderer/RayTracing/RayScene.hpp>
#include <stop_token>
#include <atomic>
#include <stdexcept>
#include <string_view>
namespace Jade::Detail {
class RenderHost;
class ProgressiveSession;
struct GpuFrame;
} // namespace Jade::Detail
namespace Jade::Renderer {
struct ProgressiveSettings : RenderSettings {
  std::uint64_t maxSamples = 0;
  std::uint64_t seed = 0;
  // Complete this many full-image rounds before applying the running-frame
  // deadline. A nonzero maxSamples also caps this minimum. Pause uses maxSamples.
  std::uint32_t minSamplesPerFrame = 4;
  bool operator==(const ProgressiveSettings&) const = default;
};
struct RenderRequest {
  CameraFrame camera;
  ImageSize size;
  std::uint64_t sampleIndex = 0;
  std::uint64_t seed = 0;
  std::stop_token cancellation;
};
class ProgressiveBackend {
public:
  virtual ~ProgressiveBackend() = default;
  virtual std::string_view name() const noexcept { return "Progressive"; }
  virtual ProgressiveSettings samplingSettings() const { return {}; }
  virtual ImageEncoding imageEncoding() const noexcept { return ImageEncoding::Radiance; }
  virtual void render(const RayScene &, const RenderRequest &, Film &) = 0;

protected:
  void assertConfigurable() const
  {
    if (m_ApplicationActive.load() || m_SessionActive.load())
      throw std::logic_error("Configure renderer algorithms outside Application::Run.");
  }

private:
  friend class Jade::Detail::RenderHost;
  friend class Jade::Detail::ProgressiveSession;
  virtual bool hasGpuFrames() const noexcept { return false; }
  virtual std::shared_ptr<Jade::Detail::GpuFrame> renderGpuFrame(const RayScene&, const RenderRequest&) { return {}; }
  std::atomic<bool> m_ApplicationActive = false;
  std::atomic<bool> m_SessionActive = false;
};
} // namespace Jade::Renderer
