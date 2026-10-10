#pragma once

#include <Jade/Assets/AssetHandle.hpp>

namespace Jade {

// A triangle mesh instance. Transform and Material are separate components.
struct Geometry {
  MeshHandle mesh;
};

} // namespace Jade
