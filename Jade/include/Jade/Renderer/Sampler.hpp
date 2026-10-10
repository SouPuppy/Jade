#pragma once

#include <Jade/Math/Vector.hpp>

#include <cstdint>

namespace Jade::Renderer {

class Sampler {
public:
  virtual ~Sampler() = default;
  virtual void start(std::uint64_t pixel, std::uint64_t sample) = 0;
  virtual float next1D() = 0;
  Vec2 next2D() { return {next1D(), next1D()}; }
};

// Counter-based stream: no per-thread RNG state affects the image.
class IndependentSampler final : public Sampler {
public:
  explicit IndependentSampler(std::uint64_t seed = 0) : m_Seed(seed) {}
  void start(std::uint64_t pixel, std::uint64_t sample) override
  {
    m_Pixel = hash(pixel);
    m_Sample = hash(sample);
    m_Dimension = 0;
  }
  float next1D() override
  {
    const auto bits = hash(m_Seed ^ m_Pixel ^ hash(m_Sample + m_Dimension++));
    return static_cast<float>(bits >> 40) * (1.0f / 16777216.0f);
  }

private:
  static std::uint64_t hash(std::uint64_t value)
  {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
  }
  std::uint64_t m_Seed, m_Pixel = 0, m_Sample = 0, m_Dimension = 0;
};

} // namespace Jade::Renderer
