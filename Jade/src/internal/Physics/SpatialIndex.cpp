#include "SpatialIndex.hpp"

namespace Jade::Detail {
namespace {
CollisionAabb emptyBounds() {
  const auto inf = std::numeric_limits<double>::infinity();
  return {{inf,inf,inf},{-inf,-inf,-inf}};
}
void include(CollisionAabb& a, const CollisionAabb& b) {
  for (unsigned k = 0; k < 3; ++k) {
    a.minimum[k] = std::min(a.minimum[k],b.minimum[k]); a.maximum[k] = std::max(a.maximum[k],b.maximum[k]);
  }
}
double area(const CollisionAabb& a) { const auto d = a.maximum-a.minimum; return 2*(d.x*d.y+d.y*d.z+d.z*d.x); }
bool encloses(const CollisionAabb& a, const CollisionAabb& b) {
  for (unsigned k = 0; k < 3; ++k) if (b.minimum[k] < a.minimum[k] || b.maximum[k] > a.maximum[k]) return false;
  return true;
}
}
void SpatialIndex::clear() noexcept { nodes.clear(); membership.clear(); order.clear(); fat.clear(); }
std::size_t SpatialIndex::split(std::size_t begin, std::size_t end) {
  constexpr unsigned bins = 16;
  auto centers = emptyBounds();
  for (auto i = begin; i < end; ++i) {
    const auto c = fat[order[i]].minimum*.5+fat[order[i]].maximum*.5; include(centers,{c,c});
  }
  double best = std::numeric_limits<double>::infinity(); unsigned axis = 0, cut = 0; bool found = false;
  for (unsigned k = 0; k < 3; ++k) {
    const double width = centers.maximum[k]-centers.minimum[k]; if (!(width > 0)) continue;
    std::array<CollisionAabb,bins> bounds; bounds.fill(emptyBounds()); std::array<std::size_t,bins> counts{};
    for (auto i = begin; i < end; ++i) {
      const auto& b = fat[order[i]];
      const auto c = (b.minimum[k]*.5+b.maximum[k]*.5-centers.minimum[k])/width;
      const auto bucket = std::min(bins-1,static_cast<unsigned>(std::max(0.0,c)*bins));
      ++counts[bucket]; include(bounds[bucket],b);
    }
    std::array<double,bins> rightCost{}; auto right = emptyBounds(); std::size_t nr = 0;
    for (unsigned j = bins; j-- > 0;) {
      if (counts[j]) include(right,bounds[j]); nr += counts[j]; rightCost[j] = nr ? area(right)*static_cast<double>(nr) : 0;
    }
    auto left = emptyBounds(); std::size_t nl = 0;
    for (unsigned j = 0; j+1 < bins; ++j) {
      if (counts[j]) include(left,bounds[j]); nl += counts[j]; if (!nl || nl == end-begin) continue;
      const double cost = area(left)*static_cast<double>(nl)+rightCost[j+1];
      if (cost < best) { best = cost; axis = k; cut = j; found = true; }
    }
  }
  if (found) {
    const auto plane = centers.minimum[axis]+(centers.maximum[axis]-centers.minimum[axis])*(cut+1)/bins;
    const auto it = std::partition(order.begin()+begin,order.begin()+end,[&](auto i) {
      return fat[i].minimum[axis]*.5+fat[i].maximum[axis]*.5 < plane;
    });
    const auto mid = static_cast<std::size_t>(it-order.begin());
    if (mid > begin && mid < end && mid-begin >= (end-begin)/16 && end-mid >= (end-begin)/16) return mid;
  }
  for (unsigned k = 1; k < 3; ++k)
    if (centers.maximum[k]-centers.minimum[k] > centers.maximum[axis]-centers.minimum[axis]) axis = k;
  const auto mid = begin+(end-begin)/2;
  std::nth_element(order.begin()+begin,order.begin()+mid,order.begin()+end,[&](auto a,auto b) {
    return fat[a].minimum[axis]*.5+fat[a].maximum[axis]*.5 < fat[b].minimum[axis]*.5+fat[b].maximum[axis]*.5;
  });
  return mid;
}
std::uint32_t SpatialIndex::build(std::size_t begin, std::size_t end) {
  const auto index = static_cast<std::uint32_t>(nodes.size()); nodes.emplace_back();
  Node n; n.bounds = emptyBounds(); for (auto i = begin; i < end; ++i) include(n.bounds,fat[order[i]]);
  if (end-begin <= 4) {
    n.leaf = true; for (auto i = begin; i < end; ++i) n.children[n.count++] = static_cast<std::uint32_t>(order[i]);
  } else {
    const auto mid = split(begin,end);
    const std::array<std::size_t,5> cuts{begin,split(begin,mid),mid,split(mid,end),end};
    for (unsigned i = 0; i < 4; ++i) if (cuts[i] < cuts[i+1]) n.children[n.count++] = build(cuts[i],cuts[i+1]);
  }
  nodes[index] = n; return index;
}
void SpatialIndex::update(std::span<const WorldCollider> shapes, std::span<const std::size_t> members, double padding,std::span<const CollisionAabb> predicted) {
  bool rebuild = membership.size() != members.size() || !std::equal(members.begin(),members.end(),membership.begin());
  if (fat.size() < shapes.size()) fat.resize(shapes.size()); bool changed = rebuild;
  for (const auto i : members) if (rebuild || !encloses(fat[i],shapes[i].bounds)) {
    const Physics::Vector p{padding,padding,padding}; fat[i] = {shapes[i].bounds.minimum-p,shapes[i].bounds.maximum+p};
    if (!predicted.empty()) include(fat[i],predicted[i]); changed = true;
  }
  if (!changed) return;
  double cost = 0;
  if (!rebuild) {
    for (auto i = nodes.size(); i-- > 0;) {
      auto& n = nodes[i]; n.bounds = emptyBounds();
      for (unsigned j = 0; j < n.count; ++j) include(n.bounds,n.leaf ? fat[n.children[j]] : nodes[n.children[j]].bounds);
      if (!n.leaf) cost += area(n.bounds);
    }
    rebuild = ++refits >= 128 && cost > buildCost*1.5;
  }
  if (rebuild) {
    membership.assign(members.begin(),members.end()); order = membership; nodes.clear(); nodes.reserve(members.size());
    if (!order.empty()) build(0,order.size()); buildCost = 0;
    for (const auto& n : nodes) if (!n.leaf) buildCost += area(n.bounds);
    refits = 0;
  }
}
} // namespace Jade::Detail
