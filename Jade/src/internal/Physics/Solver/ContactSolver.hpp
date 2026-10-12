#pragma once
#include "../Dynamics/Body.hpp"
#include "../Manifold.hpp"

namespace Jade::Physics::Internal {
V relative(const Body &a, const Body &b, V ra, V rb);
struct Constraint {
  std::size_t a, b;
  V normal, tangent1, tangent2, localA, localB;
  double normalImpulse = 0, tangentImpulse1 = 0, tangentImpulse2 = 0;
  double target = 0, friction = 0;
  V ra, rb;
  double normalMass = 0, tangentMass11 = 0, tangentMass12 = 0, tangentMass22 = 0;
  V normalA, normalB, tangent1A, tangent1B, tangent2A, tangent2B;
};
struct Cached {
  Entity a, b;
  V normal, localA, localB, tangent;
  double impulse = 0;
};
bool validAnchor(const Body &a, const Body &b, V localA, V localB, V normal, double margin);
SmallList<Constraint,4> buildConstraints(const Body &a, const Body &b, std::size_t i, std::size_t j,
                                         std::span<const Cached> previousContacts, const Config &config,
                                         float dt, double previousDt);
SmallList<Detail::PreciseContactPoint,24> persistentPoints(const Body &a, const Body &b, const Detail::CollisionManifold &hit,
                                           std::span<const Cached> cache, double margin);
void prepareConstraint(std::span<Body> bodies, Constraint& constraint);
void warmConstraint(std::span<Body> bodies, Constraint& constraint);
void solveVelocity(std::span<Body> bodies, Constraint& constraint);
void solvePosition(std::span<Body> bodies, const Constraint& constraint, const Config& config);
SolverBackend selectBackend(SolverBackend requested);
void solveVelocityPacket(std::span<Body> bodies, std::array<Constraint*,4> constraints, SolverBackend backend);
void normalFourAvx2(const double* old,const double* target,const double* speed,const double* mass,double* result);
void applyVelocityImpulse(std::span<Body> bodies, const Constraint& c,double normal,double tangent1,double tangent2);
} // namespace Jade::Physics::Internal
