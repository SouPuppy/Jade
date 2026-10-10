#pragma once

#include <Jade/Log.hpp>

#include <iosfwd>

namespace Jade::Detail {

void WriteLog(std::ostream& output, std::string_view prefix, std::string_view message);

} // namespace Jade::Detail
