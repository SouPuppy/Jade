#pragma once
#include "CollisionWorld.hpp"
#include <Jade/Core/JobSystem.hpp>
#include <span>

namespace Jade::Detail {
struct ColliderPair {
  std::size_t a = 0, b = 0;
  friend bool operator==(const ColliderPair&, const ColliderPair&) = default;
};
// Stable indices into input shapes. An optional dynamic mask excludes static/static.
std::vector<ColliderPair> broadPhase(std::span<const WorldCollider> shapes, double margin,
                                    JobSystem* jobs = nullptr, std::span<const unsigned char> dynamic = {});
} // namespace Jade::Detail
