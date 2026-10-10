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
} // namespace Jade::Detail
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
class ProgressiveBackend {
public:
  virtual ~ProgressiveBackend() = default;
  virtual std::string_view name() const noexcept { return "Progressive"; }
  virtual ProgressiveSettings samplingSettings() const { return {}; }
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
  std::atomic<bool> m_ApplicationActive = false;
  std::atomic<bool> m_SessionActive = false;
};
} // namespace Jade::Renderer
