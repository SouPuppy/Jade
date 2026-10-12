#include "AllocationCounter.hpp"
#include <atomic>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
namespace {
std::atomic<bool> enabled{false};
std::atomic<std::uint64_t> calls{0},bytes{0};
void record(std::size_t size) noexcept {
  if (enabled.load(std::memory_order_relaxed)) { calls.fetch_add(1,std::memory_order_relaxed); bytes.fetch_add(size,std::memory_order_relaxed); }
}
}
namespace BenchmarkAllocation {
void start() noexcept { calls = 0; bytes = 0; enabled = true; }
Counts stop() noexcept { enabled = false; return {calls.load(),bytes.load()}; }
}
void* operator new(std::size_t size) { if (auto* p = std::malloc(size ? size : 1)) { record(size); return p; } throw std::bad_alloc(); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t size,std::align_val_t alignment) {
  const auto align = static_cast<std::size_t>(alignment);
#ifdef _WIN32
  auto* p = _aligned_malloc(size ? size : 1,align);
#else
  auto* p = std::aligned_alloc(align,((size ? size : 1)+align-1)/align*align);
#endif
  if (!p) throw std::bad_alloc(); record(size); return p;
}
void* operator new[](std::size_t size,std::align_val_t alignment) { return ::operator new(size,alignment); }
void operator delete(void* p,std::align_val_t) noexcept {
#ifdef _WIN32
  _aligned_free(p);
#else
  std::free(p);
#endif
}
void operator delete[](void* p,std::align_val_t alignment) noexcept { ::operator delete(p,alignment); }
void operator delete(void* p,std::size_t,std::align_val_t alignment) noexcept { ::operator delete(p,alignment); }
void operator delete[](void* p,std::size_t,std::align_val_t alignment) noexcept { ::operator delete(p,alignment); }
void* operator new(std::size_t size,const std::nothrow_t&) noexcept { try { return ::operator new(size); } catch (...) { return nullptr; } }
void* operator new[](std::size_t size,const std::nothrow_t&) noexcept { try { return ::operator new[](size); } catch (...) { return nullptr; } }
void* operator new(std::size_t size,std::align_val_t alignment,const std::nothrow_t&) noexcept { try { return ::operator new(size,alignment); } catch (...) { return nullptr; } }
void* operator new[](std::size_t size,std::align_val_t alignment,const std::nothrow_t&) noexcept { try { return ::operator new[](size,alignment); } catch (...) { return nullptr; } }
void operator delete(void* p,const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p,const std::nothrow_t&) noexcept { ::operator delete[](p); }
void operator delete(void* p,std::align_val_t alignment,const std::nothrow_t&) noexcept { ::operator delete(p,alignment); }
void operator delete[](void* p,std::align_val_t alignment,const std::nothrow_t&) noexcept { ::operator delete[](p,alignment); }
