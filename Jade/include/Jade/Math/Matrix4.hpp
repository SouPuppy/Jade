#pragma once

#include <array>
#include <cstddef>

namespace Jade {

// Column-major storage. Default is zero; Identity() supplies the unit matrix.
struct Matrix4 {
  std::array<float, 16> values{};
  float& at(std::size_t row, std::size_t column) noexcept { return values[column * 4 + row]; }
  float at(std::size_t row, std::size_t column) const noexcept { return values[column * 4 + row]; }
  const float* data() const noexcept { return values.data(); }
  static Matrix4 Identity() noexcept
  {
    Matrix4 result;
    for (std::size_t index = 0; index < 4; ++index) result.at(index, index) = 1;
    return result;
  }
};

Matrix4 operator*(const Matrix4& a, const Matrix4& b);

} // namespace Jade
