#pragma once
#include <Jade/Renderer/Common/Image.hpp>
#include <atomic>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
namespace Jade::Detail { class RenderHost; class ProgressiveSession; }
namespace Jade::Renderer {
enum class Execution { Progressive, Frame };
struct RenderSettings {
  float resolutionScale = 1;
  float exposureEV = 0;
};
struct RenderProgress {
  std::uint64_t revision = 0;
  std::optional<std::uint64_t> samplesPerPixel;
  ImageSize size;
  double sampleMilliseconds = 0;
  bool inFlight = false;
  std::string rendererName;
  std::uint64_t completedFrames = 0;
  double frameMilliseconds = 0;
};
class Backend {
public:
  virtual ~Backend() = default;
  virtual std::string_view name() const noexcept = 0;
  virtual Execution execution() const noexcept = 0;
  virtual RenderSettings settings() const { return {}; }
protected:
  void assertConfigurable() const {
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
