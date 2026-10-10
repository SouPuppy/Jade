#include "Image.hpp"
#include "Math.hpp"

#include "../../internal/Math/Matrix4.hpp"

#include <limits>
#include <stdexcept>

namespace Jade::Renderer {

std::size_t ImageSize::pixels() const
{
  if (width <= 0 || height <= 0)
    throw std::invalid_argument("Image dimensions must be positive.");
  const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (count > std::vector<LinearRGB>().max_size() || count > std::vector<std::uint8_t>().max_size() / 4)
    throw std::invalid_argument("Image dimensions exceed storage limits.");
  return count;
}

Film::Film(ImageSize size) : m_Size(size), m_Mean(size.pixels()) {}

void Film::addSample(std::span<const LinearRGB> sample)
{
  if (sample.size() != m_Mean.size())
    throw std::invalid_argument("Sample dimensions differ from Film.");
  if (m_Samples == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("Film sample count overflow.");
  for (const auto value : sample) {
    if (!value.finite() || value.r < 0 || value.g < 0 || value.b < 0)
      throw std::runtime_error("Integrator produced invalid radiance.");
  }
  const double weight = 1.0 / static_cast<double>(m_Samples + 1);
  for (std::size_t i = 0; i < sample.size(); ++i) {
    auto &mean = m_Mean[i];
    const auto s = sample[i];
    mean.r = static_cast<float>(mean.r * (1 - weight) + s.r * weight);
    mean.g = static_cast<float>(mean.g * (1 - weight) + s.g * weight);
    mean.b = static_cast<float>(mean.b * (1 - weight) + s.b * weight);
  }
  ++m_Samples;
}

Image Film::resolve(float exposureEV) const
{
  if (!std::isfinite(exposureEV) || std::abs(exposureEV) > 64)
    throw std::invalid_argument("Exposure must be finite and within [-64, 64] EV.");
  Image result{m_Size, std::vector<std::uint8_t>(m_Mean.size() * 4)};
  const double exposure = std::exp2(static_cast<double>(exposureEV));
  const auto encode = [&](float radiance) {
    const double linear = radiance * exposure;
    const double mapped = linear / (1 + linear);
    const double srgb = mapped <= 0.0031308 ? 12.92 * mapped : 1.055 * std::pow(mapped, 1.0 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(std::clamp(std::lround(srgb * 255), 0L, 255L));
  };
  for (std::size_t i = 0; i < m_Mean.size(); ++i) {
    result.pixels[i * 4] = encode(m_Mean[i].r);
    result.pixels[i * 4 + 1] = encode(m_Mean[i].g);
    result.pixels[i * 4 + 2] = encode(m_Mean[i].b);
    result.pixels[i * 4 + 3] = 255;
  }
  return result;
}

} // namespace Jade::Renderer
