#pragma once

#include <Jade/Graphics/MeshData.hpp>

#include <filesystem>

namespace Jade::Detail {

MeshData LoadObj(const std::filesystem::path& path);

} // namespace Jade::Detail
