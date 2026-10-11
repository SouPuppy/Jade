#include "BroadPhase.hpp"
#include <algorithm>
#include <numeric>

namespace Jade::Detail {
std::vector<ColliderPair> broadPhase(std::span<const WorldCollider> shapes, double margin,
                                    JobSystem* jobs, std::span<const unsigned char> dynamic) {
  if (!std::isfinite(margin) || margin < 0 || (!dynamic.empty() && dynamic.size() != shapes.size()))
    throw std::invalid_argument("Invalid broad phase margin or dynamic mask.");
  std::vector<std::size_t> order(shapes.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::sort(order.begin(), order.end(), [&](auto a, auto b) {
    const auto x = shapes[a].bounds.minimum.x, y = shapes[b].bounds.minimum.x;
    return x == y ? a < b : x < y;
  });
  std::vector<std::vector<ColliderPair>> output(order.size());
  const auto scan = [&](std::size_t index) {
    const auto a = order[index];
    for (auto next = index + 1; next < order.size(); ++next) {
      const auto b = order[next];
      if (shapes[a].bounds.maximum.x + margin < shapes[b].bounds.minimum.x) break;
      if (!dynamic.empty() && !dynamic[a] && !dynamic[b]) continue;
      if (shapes[a].bounds.overlaps(shapes[b].bounds, margin))
        output[index].push_back({std::min(a, b), std::max(a, b)});
    }
  };
  if (jobs) jobs->wait(jobs->parallelFor(order.size(), scan, {JobCategory::Physics}), WaitMode::Help);
  else for (std::size_t i = 0; i < order.size(); ++i) scan(i);
  std::size_t count = 0;
  for (const auto& pairs : output) count += pairs.size();
  std::vector<ColliderPair> pairs; pairs.reserve(count);
  for (auto& list : output) pairs.insert(pairs.end(), list.begin(), list.end());
  std::sort(pairs.begin(), pairs.end(), [](auto a, auto b) { return a.a == b.a ? a.b < b.b : a.a < b.a; });
  return pairs;
}
} // namespace Jade::Detail
