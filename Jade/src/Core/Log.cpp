#include <Jade/Core/Log.h>

#include <iostream>

namespace Jade::Log {

void Info(std::string_view message)
{
  std::cout << "[Jade] " << message << '\n';
}

void Error(std::string_view message)
{
  std::cerr << "[Jade] Error: " << message << '\n';
}

} // namespace Jade::Log
