#pragma once
#include "../Dynamics/Body.hpp"

namespace Jade::Physics::Internal {
V relative(const Body &a, const Body &b, V ra, V rb);
struct Constraint {
  std::size_t a, b;
  V normal, tangent1, tangent2, localA, localB;
  double normalImpulse = 0, tangentImpulse1 = 0, tangentImpulse2 = 0;
  double target = 0, friction = 0;
};
struct Cached {
  Entity a, b;
  V normal, localA, localB, tangent;
  double impulse = 0;
};
bool validAnchor(const Body &a, const Body &b, V localA, V localB, V normal, double margin);
std::vector<Constraint> buildConstraints(const Body &a, const Body &b, std::size_t i, std::size_t j,
                                         std::span<const Cached> previousContacts, const Config &config,
                                         float dt, double previousDt);
std::vector<std::vector<std::size_t>> contactIslands(std::span<const Constraint> constraints,
                                                     std::span<const unsigned char> dynamic);
std::vector<ContactPoint> persistentPoints(const Body &a, const Body &b, const Manifold &hit,
                                           std::span<const Cached> cache, double margin);
void solveIsland(std::span<Body> bodies, std::span<Constraint> constraints,
                 std::span<const std::size_t> island, const Config &config);
} // namespace Jade::Physics::Internal
