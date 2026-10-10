#pragma once

#include <Jade/Graphics/Mesh.hpp>

#include <filesystem>

namespace Jade::Detail {

Mesh LoadObj(const std::filesystem::path& path);

} // namespace Jade::Detail
