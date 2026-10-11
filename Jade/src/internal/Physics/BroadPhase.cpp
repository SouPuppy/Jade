#include "BroadPhase.hpp"
#include "SpatialIndex.hpp"
#include <algorithm>
#include <numeric>

namespace Jade::Detail {
std::vector<ColliderPair> broadPhase(std::span<const WorldCollider> shapes, double margin,
                                    JobSystem* jobs, std::span<const unsigned char> dynamic) {
  if (!std::isfinite(margin) || margin < 0 || (!dynamic.empty() && dynamic.size() != shapes.size()))
    throw std::invalid_argument("Invalid broad phase margin or dynamic mask.");
  std::vector<std::size_t> order(shapes.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  SpatialIndex tree; tree.update(shapes, order, 0);
  constexpr std::size_t batch = 128;
  std::vector<std::vector<ColliderPair>> output((order.size()+batch-1)/batch);
  const auto scan = [&](std::size_t index) {
    for (auto a = index*batch; a < std::min(shapes.size(),(index+1)*batch); ++a)
      tree.query(shapes[a].bounds,margin,[&](auto b) {
        if (b <= a || (!dynamic.empty() && !dynamic[a] && !dynamic[b])) return;
        if (shapes[a].bounds.overlaps(shapes[b].bounds,margin)) output[index].push_back({a,b});
      });
  };
  if (jobs && output.size() > 1) jobs->wait(jobs->parallelFor(output.size(),scan,{JobCategory::Physics,{},1}),WaitMode::Help);
  else for (std::size_t i = 0; i < output.size(); ++i) scan(i);
  std::size_t count = 0;
  for (const auto& pairs : output) count += pairs.size();
  std::vector<ColliderPair> pairs; pairs.reserve(count);
  for (auto& list : output) pairs.insert(pairs.end(), list.begin(), list.end());
  std::sort(pairs.begin(), pairs.end(), [](auto a, auto b) { return a.a == b.a ? a.b < b.b : a.a < b.a; });
  return pairs;
}
} // namespace Jade::Detail
