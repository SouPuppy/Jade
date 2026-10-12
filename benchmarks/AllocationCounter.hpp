#pragma once
#include <cstdint>
namespace BenchmarkAllocation {
struct Counts { std::uint64_t calls = 0, bytes = 0; };
void start() noexcept;
Counts stop() noexcept;
}
