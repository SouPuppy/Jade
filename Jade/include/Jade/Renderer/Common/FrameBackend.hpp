#pragma once
#include <Jade/Renderer/Common/Backend.hpp>
#include <Jade/Renderer/Common/SceneSnapshot.hpp>
namespace Jade::Renderer {
class FrameTarget {
public:
  virtual ~FrameTarget() = default;
  virtual ImageSize size() const noexcept = 0;
  virtual void clear(LinearRGB color) = 0;
  // Top-left origin. Uploads a complete finite nonnegative linear HDR image.
  virtual void upload(std::span<const LinearRGB> pixels) = 0;
};
struct FrameRequest {
  ImageSize size;
  std::uint64_t revision = 0;
  std::uint64_t frameIndex = 0;
};
class FrameBackend : public Backend {
public:
  std::string_view name() const noexcept override { return "Frame"; }
  Execution execution() const noexcept final { return Execution::Frame; }
  // Target is borrowed for this call; implementations run on its owning thread.
  virtual void render(const SceneSnapshot &, const FrameRequest &, FrameTarget &) = 0;
};
} // namespace Jade::Renderer
