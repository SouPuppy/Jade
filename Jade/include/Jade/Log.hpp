#pragma once

#include <string_view>

namespace Jade::Log {

void Info(std::string_view message);
void Error(std::string_view message);

} // namespace Jade::Log
