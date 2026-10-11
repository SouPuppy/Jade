#pragma once
#include "CollisionWorld.hpp"
#include <array>
#include <span>

namespace Jade::Detail {
// Four-way persistent tree. Writes finish before queries begin.
class SpatialIndex {
public:
  void clear() noexcept;
  void update(std::span<const WorldCollider> shapes, std::span<const std::size_t> members, double padding);
  template<class Visitor> void query(const CollisionAabb& bounds, double margin, Visitor&& visit) const {
    if (nodes.empty()) return;
    std::array<std::uint32_t,256> stack{}; std::size_t count = 1;
    while (count) {
      const auto& n = nodes[stack[--count]];
      if (!n.bounds.overlaps(bounds,margin)) continue;
      for (unsigned i = 0; i < n.count; ++i) {
        if (n.leaf) visit(n.children[i]);
        else {
          if (count == stack.size()) throw std::logic_error("Spatial tree depth exceeded.");
          stack[count++] = n.children[i];
        }
      }
    }
  }
private:
  struct Node { CollisionAabb bounds; std::array<std::uint32_t,4> children{}; unsigned count = 0; bool leaf = false; };
  std::vector<Node> nodes;
  std::vector<std::size_t> membership, order;
  std::vector<CollisionAabb> fat;
  double buildCost = 0;
  unsigned refits = 0;
  std::uint32_t build(std::size_t begin, std::size_t end);
  std::size_t split(std::size_t begin, std::size_t end);
};
} // namespace Jade::Detail
