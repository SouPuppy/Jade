#pragma once

#include <Jade/Renderer/RenderScene.hpp>

#include <stop_token>
#include <atomic>
#include <stdexcept>

namespace Jade::Detail {
class RenderSession;
}

namespace Jade::Renderer {

struct RenderSettings {
  float resolutionScale = 1;
  std::uint64_t maxSamples = 0;
  std::uint64_t seed = 0;
  float exposureEV = 0;
};

struct RenderRequest {
  CameraFrame camera;
  ImageSize size;
  std::uint64_t sampleIndex = 0;
  std::uint64_t seed = 0;
  std::stop_token cancellation;
};

struct RenderProgress {
  std::uint64_t revision = 0;
  std::uint64_t samplesPerPixel = 0;
  ImageSize size;
  double sampleMilliseconds = 0;
  bool inFlight = false;
};

// Headless computation only: one full sample round per call. Concurrent calls
// to the same backend are not supported. Integrators must permit concurrent Li.
class Backend {
public:
  virtual ~Backend() = default;
  virtual RenderSettings settings() const { return {}; }
  virtual void render(const RenderScene &scene, const RenderRequest &request, Film &film) = 0;

protected:
  void assertConfigurable() const
  {
    if (m_SessionActive.load())
      throw std::logic_error("Configure renderer algorithms outside Application::Run.");
  }

private:
  friend class Jade::Detail::RenderSession;
  std::atomic<bool> m_SessionActive = false;
};

} // namespace Jade::Renderer
