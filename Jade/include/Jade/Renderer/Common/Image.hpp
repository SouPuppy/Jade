#pragma once

#include <Jade/Renderer/Common/LinearRGB.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace Jade::Renderer {

struct ImageSize {
  int width = 0;
  int height = 0;
  bool operator==(const ImageSize &) const = default;
  std::size_t pixels() const;
};

// Top-left origin, tightly packed sRGB RGBA8, alpha always opaque.
struct Image {
  ImageSize size;
  std::vector<std::uint8_t> pixels;
};

class Film final {
public:
  explicit Film(ImageSize size);
  ImageSize size() const noexcept { return m_Size; }
  std::uint64_t samples() const noexcept { return m_Samples; }
  std::span<const LinearRGB> pixels() const noexcept { return m_Mean; }
  // Commits a complete round atomically with respect to cancellation.
  void addSample(std::span<const LinearRGB> sample);
  Image resolve(float exposureEV = 0) const;

private:
  ImageSize m_Size;
  std::uint64_t m_Samples = 0;
  std::vector<LinearRGB> m_Mean;
};

} // namespace Jade::Renderer
