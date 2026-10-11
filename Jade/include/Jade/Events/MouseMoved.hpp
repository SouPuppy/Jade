#pragma once

namespace Jade {

struct MouseMoved {
  // Window coordinates: X right, Y down. First movement after focus has zero delta.
  double x = 0;
  double y = 0;
  double dx = 0;
  double dy = 0;
  // Height in the same window-coordinate units as dx/dy; zero if unavailable.
  int windowHeight = 0;
};

} // namespace Jade
