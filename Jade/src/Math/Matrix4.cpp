#include "Matrix4.hpp"

#include "../internal/Math/Validation.hpp"

namespace Jade {

Matrix4 operator*(const Matrix4& a, const Matrix4& b)
{
  Matrix4 result;
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      double value = 0;
      for (std::size_t index = 0; index < 4; ++index) {
        value += static_cast<double>(a.at(row, index)) * b.at(index, column);
      }
      result.at(row, column) = static_cast<float>(value);
      if (!std::isfinite(result.at(row, column))) {
        throw std::invalid_argument("Transform exceeds the supported floating-point range.");
      }
    }
  }
  return result;
}

} // namespace Jade
