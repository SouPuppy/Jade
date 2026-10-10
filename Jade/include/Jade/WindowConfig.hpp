#pragma once

#include <string>

namespace Jade {

struct WindowConfig {
  std::string title = "Jade";
  int width = 960;
  int height = 640;
  bool visible = true;
};

} // namespace Jade
