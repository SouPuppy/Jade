#pragma once

#include <Jade/Mesh/Mesh.hpp>

namespace Jade::Detail {

Mesh LoadObj(const std::filesystem::path& path);

} // namespace Jade::Detail
