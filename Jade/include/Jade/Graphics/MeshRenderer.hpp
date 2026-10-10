#pragma once

#include <Jade/Assets/AssetHandle.hpp>
#include <Jade/Color.hpp>

namespace Jade {

struct MeshRenderer {
  MeshHandle mesh;
  Color color{1, 1, 1, 1};
};

} // namespace Jade
