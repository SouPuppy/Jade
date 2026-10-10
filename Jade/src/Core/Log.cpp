#include "Log.hpp"

#include <iostream>

namespace Jade::Detail {

void WriteLog(std::ostream& output, std::string_view prefix, std::string_view message)
{
  output << prefix << message << '\n';
}

} // namespace Jade::Detail

namespace Jade::Log {

void Info(std::string_view message)
{
  Detail::WriteLog(std::cout, "[Jade] ", message);
}

void Error(std::string_view message)
{
  Detail::WriteLog(std::cerr, "[Jade] Error: ", message);
}

} // namespace Jade::Log
