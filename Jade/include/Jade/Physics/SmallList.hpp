#pragma once
#include <array>
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <utility>

namespace Jade::Physics {
// Bounded geometric output. Overflow is an error, never silent contact loss.
template<class T, std::size_t Capacity> class SmallList {
public:
  SmallList() = default;
  SmallList(std::initializer_list<T> values) { for (const auto& v : values) push_back(v); }
  std::size_t size() const noexcept { return count; }
  bool empty() const noexcept { return count == 0; }
  T* data() noexcept { return storage.data(); }
  const T* data() const noexcept { return storage.data(); }
  T* begin() noexcept { return data(); }
  T* end() noexcept { return data()+count; }
  const T* begin() const noexcept { return data(); }
  const T* end() const noexcept { return data()+count; }
  T& operator[](std::size_t i) { return storage[i]; }
  const T& operator[](std::size_t i) const { return storage[i]; }
  T& front() { return storage[0]; }
  const T& front() const { return storage[0]; }
  T& back() { return storage[count-1]; }
  const T& back() const { return storage[count-1]; }
  void clear() noexcept { count = 0; }
  void push_back(const T& value) {
    if (count == Capacity) throw std::length_error("Physics geometric buffer overflow.");
    storage[count++] = value;
  }
  T* erase(T* item) { std::move(item+1,end(),item); --count; return item; }
private:
  std::array<T,Capacity> storage{};
  std::size_t count = 0;
};
} // namespace Jade::Physics
