#pragma once
#include <Jade/Renderer/RayTracing/Ray.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stop_token>
#include <span>
#include <vector>
namespace Jade::Detail {
struct BuildCancelled {};
inline void CheckCancellation(std::stop_token token)
{
  if (token.stop_requested())
    throw BuildCancelled{};
}
inline float Coordinate(Vec3 v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
struct Bounds {
  Vec3 low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
  bool valid() const { return low.x <= high.x && low.y <= high.y && low.z <= high.z; }
  void add(Vec3 p)
  {
    low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
    high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
  }
  void add(const Bounds &other)
  {
    if (other.valid()) {
      add(other.low);
      add(other.high);
    }
  }
  Vec3 center() const { return low * 0.5f + high * 0.5f; }
  double area() const
  {
    if (!valid())
      return 0;
    const double x = static_cast<double>(high.x) - low.x;
    const double y = static_cast<double>(high.y) - low.y;
    const double z = static_cast<double>(high.z) - low.z;
    return 2 * (x * y + y * z + z * x);
  }
  bool hit(const Renderer::Ray &ray, float maximum, double &entry) const;
};
struct TraversalStats {
  std::uint64_t boundsTests = 0, primitiveTests = 0;
};
struct BVHStats {
  std::size_t nodes = 0, primitives = 0;
  std::uint32_t depth = 0;
  double cost = 0;
};
// Shared binary binned-SAH implementation for BLAS and TLAS.
class BVH final {
public:
  struct Node {
    Bounds bounds;
    std::uint32_t start = 0, count = 0, left = 0, right = 0;
  };
  std::span<const Node> nodes() const noexcept { return m_Nodes; }
  std::span<const std::uint32_t> order() const noexcept { return m_Order; }
  Bounds bounds() const { return m_Nodes.empty() ? Bounds{} : m_Nodes.front().bounds; }
  void build(const std::vector<Bounds> &bounds, std::stop_token token = {});
  void refit(const std::vector<Bounds> &bounds, std::stop_token token = {});
  BVHStats statistics() const { return m_Stats; }
  double buildCost() const { return m_BuildCost; }
  template <class Callback>
  bool visit(const Renderer::Ray &ray, float &maximum, bool any, Callback &&callback,
             TraversalStats *stats = nullptr) const
  {
    if (m_Nodes.empty())
      return false;
    struct Pending {
      std::uint32_t node;
      double entry;
    };
    std::array<Pending, 64> stack{};
    double entry;
    if (stats)
      ++stats->boundsTests;
    if (!bounds().hit(ray, maximum, entry))
      return false;
    stack[0] = {0, entry};
    std::size_t pending = 1;
    bool found = false;
    while (pending) {
      const auto next = stack[--pending];
      if (next.entry > maximum)
        continue;
      const auto &node = m_Nodes[next.node];
      if (node.count) {
        for (std::uint32_t j = 0; j < node.count; ++j) {
          if (stats)
            ++stats->primitiveTests;
          if (callback(m_Order[node.start + j])) {
            found = true;
            if (any)
              return true;
          }
        }
      } else {
        double left, right;
        if (stats)
          stats->boundsTests += 2;
        const bool hitLeft = m_Nodes[node.left].bounds.hit(ray, maximum, left);
        const bool hitRight = m_Nodes[node.right].bounds.hit(ray, maximum, right);
        if (hitLeft && hitRight) {
          if (left <= right) {
            stack[pending++] = {node.right, right};
            stack[pending++] = {node.left, left};
          } else {
            stack[pending++] = {node.left, left};
            stack[pending++] = {node.right, right};
          }
        } else if (hitLeft)
          stack[pending++] = {node.left, left};
        else if (hitRight)
          stack[pending++] = {node.right, right};
      }
    }
    return found;
  }

private:
  std::uint32_t buildNode(std::size_t first, std::size_t count, const std::vector<Bounds> &bounds,
                          std::uint32_t depth, std::stop_token token);
  void updateCost();
  std::vector<Node> m_Nodes;
  std::vector<std::uint32_t> m_Order;
  BVHStats m_Stats;
  double m_BuildCost = 0;
};
} // namespace Jade::Detail
