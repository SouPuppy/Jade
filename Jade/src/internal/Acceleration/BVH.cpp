#include "BVH.hpp"
#include <limits>
#include <stdexcept>
namespace Jade::Detail {
bool Bounds::hit(const Renderer::Ray &ray, float maximum, double &entry) const
{
  if (!valid())
    return false;
  double near = ray.tMin, far = std::min(ray.tMax, maximum);
  if (near > far)
    return false;
  for (int axis = 0; axis < 3; ++axis) {
    const double origin = Coordinate(ray.origin, axis), direction = Coordinate(ray.direction, axis);
    // Expand in double so finite float extrema do not become infinite bounds.
    const double lo = std::nextafter(static_cast<double>(Coordinate(low, axis)), -INFINITY);
    const double hi = std::nextafter(static_cast<double>(Coordinate(high, axis)), INFINITY);
    if (direction == 0) {
      if (origin < lo || origin > hi)
        return false;
      continue;
    }
    double a = (lo - origin) / direction, b = (hi - origin) / direction;
    if (a > b)
      std::swap(a, b);
    near = std::max(near, a);
    far = std::min(far, b);
    if (near > far)
      return false;
  }
  entry = near;
  return true;
}
std::uint32_t BVH::buildNode(std::size_t first, std::size_t count, const std::vector<Bounds> &bounds,
                             std::uint32_t depth, std::stop_token token)
{
  CheckCancellation(token);
  if (m_Nodes.size() >= std::numeric_limits<std::uint32_t>::max())
    throw std::overflow_error("BVH node count overflow.");
  const auto index = static_cast<std::uint32_t>(m_Nodes.size());
  m_Nodes.emplace_back();
  m_Stats.depth = std::max(m_Stats.depth, depth);
  Bounds box, centroids;
  for (std::size_t i = first; i < first + count; ++i) {
    if ((i & 1023) == 0)
      CheckCancellation(token);
    box.add(bounds[m_Order[i]]);
    centroids.add(bounds[m_Order[i]].center());
  }
  m_Nodes[index].bounds = box;
  auto leaf = [&] {
    m_Nodes[index].start = static_cast<std::uint32_t>(first);
    m_Nodes[index].count = static_cast<std::uint32_t>(count);
    return index;
  };
  if (count == 1 || depth == 48)
    return leaf();
  constexpr int bins = 16;
  struct Bucket {
    Bounds bounds;
    std::size_t count = 0;
  };
  double best = INFINITY;
  int bestAxis = -1, bestSplit = -1;
  const double area = box.area();
  auto bucketOf = [&](std::uint32_t primitive, int axis) {
    const double lo = Coordinate(centroids.low, axis);
    const double extent = static_cast<double>(Coordinate(centroids.high, axis)) - lo;
    const double fraction = (static_cast<double>(Coordinate(bounds[primitive].center(), axis)) - lo) / extent;
    return std::clamp(static_cast<int>(fraction * bins), 0, bins - 1);
  };
  if (area > 0)
    for (int axis = 0; axis < 3; ++axis) {
      if (Coordinate(centroids.high, axis) == Coordinate(centroids.low, axis))
        continue;
      std::array<Bucket, bins> buckets{};
      for (std::size_t i = first; i < first + count; ++i) {
        if ((i & 1023) == 0)
          CheckCancellation(token);
        auto &bucket = buckets[bucketOf(m_Order[i], axis)];
        ++bucket.count;
        bucket.bounds.add(bounds[m_Order[i]]);
      }
      std::array<Bounds, bins> leftBounds, rightBounds;
      std::array<std::size_t, bins> leftCounts{}, rightCounts{};
      Bounds left, right;
      std::size_t lc = 0, rc = 0;
      for (int i = 0; i < bins; ++i) {
        left.add(buckets[i].bounds);
        lc += buckets[i].count;
        leftBounds[i] = left;
        leftCounts[i] = lc;
        const int j = bins - 1 - i;
        right.add(buckets[j].bounds);
        rc += buckets[j].count;
        rightBounds[j] = right;
        rightCounts[j] = rc;
      }
      for (int split = 0; split < bins - 1; ++split) {
        if (!leftCounts[split] || !rightCounts[split + 1])
          continue;
        const double cost = 1 + (leftBounds[split].area() * leftCounts[split] +
                                 rightBounds[split + 1].area() * rightCounts[split + 1]) /
                                    area;
        if (cost < best) {
          best = cost;
          bestAxis = axis;
          bestSplit = split;
        }
      }
    }
  if (count <= 4 && best >= static_cast<double>(count))
    return leaf();
  std::size_t middle = first + count / 2;
  if (bestAxis >= 0) {
    // Stable partition makes ties reproducible and preserves original primitive IDs.
    auto it = std::stable_partition(m_Order.begin() + static_cast<std::ptrdiff_t>(first),
                                    m_Order.begin() + static_cast<std::ptrdiff_t>(first + count),
                                    [&](auto p) { return bucketOf(p, bestAxis) <= bestSplit; });
    middle = static_cast<std::size_t>(it - m_Order.begin());
  }
  if (bestAxis < 0 || middle == first || middle == first + count) {
    std::sort(m_Order.begin() + static_cast<std::ptrdiff_t>(first),
              m_Order.begin() + static_cast<std::ptrdiff_t>(first + count));
    middle = first + count / 2;
  }
  const auto left = buildNode(first, middle - first, bounds, depth + 1, token);
  const auto right = buildNode(middle, first + count - middle, bounds, depth + 1, token);
  m_Nodes[index].left = left;
  m_Nodes[index].right = right;
  return index;
}
void BVH::updateCost()
{
  std::vector<double> costs(m_Nodes.size());
  for (std::size_t i = m_Nodes.size(); i-- > 0;) {
    const auto &node = m_Nodes[i];
    if (node.count)
      costs[i] = node.count;
    else {
      const double area = node.bounds.area();
      costs[i] = area > 0 ? 1 + (m_Nodes[node.left].bounds.area() * costs[node.left] +
                                 m_Nodes[node.right].bounds.area() * costs[node.right]) /
                                    area
                          : 1 + costs[node.left] + costs[node.right];
    }
  }
  m_Stats.nodes = m_Nodes.size();
  m_Stats.primitives = m_Order.size();
  m_Stats.cost = costs.empty() ? 0 : costs.front();
}
void BVH::build(const std::vector<Bounds> &bounds, std::stop_token token)
{
  if (bounds.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument("BVH primitive count overflow.");
  // Build transactionally; cancellation never replaces a valid tree.
  BVH next;
  for (std::size_t i = 0; i < bounds.size(); ++i) {
    if ((i & 1023) == 0)
      CheckCancellation(token);
    if (bounds[i].valid())
      next.m_Order.push_back(static_cast<std::uint32_t>(i));
  }
  if (!next.m_Order.empty())
    next.buildNode(0, next.m_Order.size(), bounds, 0, token);
  next.updateCost();
  next.m_BuildCost = next.m_Stats.cost;
  CheckCancellation(token);
  *this = std::move(next);
}
void BVH::refit(const std::vector<Bounds> &bounds, std::stop_token token)
{
  BVH next = *this;
  for (std::size_t i = next.m_Nodes.size(); i-- > 0;) {
    CheckCancellation(token);
    auto &node = next.m_Nodes[i];
    Bounds box;
    if (node.count) {
      for (std::uint32_t j = 0; j < node.count; ++j) {
        if ((j & 1023) == 0)
          CheckCancellation(token);
        const auto primitive = next.m_Order[node.start + j];
        if (primitive >= bounds.size() || !bounds[primitive].valid())
          throw std::invalid_argument("Refit requires the same valid primitive set.");
        box.add(bounds[primitive]);
      }
    } else {
      box.add(next.m_Nodes[node.left].bounds);
      box.add(next.m_Nodes[node.right].bounds);
    }
    node.bounds = box;
  }
  next.updateCost();
  CheckCancellation(token);
  *this = std::move(next);
}
} // namespace Jade::Detail
