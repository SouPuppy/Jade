#pragma once

#include <Jade/Assets/AssetHandle.hpp>
#include <Jade/Math/Vector.hpp>
#include <Jade/Renderer/Common/LinearRGB.hpp>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace Jade {
struct Texel {
  Renderer::LinearRGB color;
  float alpha = 0;
};

// Immutable after registration with Assets. RGBA8 sRGB, rows from top to bottom.
class Texture final {
public:
  Texture(int width, int height, std::vector<std::uint8_t> rgba);
  static Texture loadPng(const std::filesystem::path& path);
  int width() const noexcept { return m_Width; }
  int height() const noexcept { return m_Height; }
  const std::vector<std::uint8_t>& pixels() const noexcept { return m_Pixels; }
  // Nearest, clamped sampling; UV origin is the top-left pixel.
  Texel sample(Vec2 uv) const;
private:
  int m_Width, m_Height;
  std::vector<std::uint8_t> m_Pixels;
};
using TextureHandle = AssetHandle<Texture>;
} // namespace Jade
