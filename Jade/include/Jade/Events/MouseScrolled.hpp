#pragma once

namespace Jade {

struct MouseScrolled {
  // Scroll offsets, including fractional touchpad input. Positive dy zooms in.
  double dx = 0;
  double dy = 0;
};

} // namespace Jade
