#pragma once

#include <cstdint>

namespace Jade {

struct FrameUpdate {
  float dt = 0;
  double elapsed = 0;
  std::uint64_t frame = 0;
};

} // namespace Jade
