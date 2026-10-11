#pragma once
#include <Jade/Core/Scene.hpp>
#include <Jade/Physics/RigidBody.hpp>
#include <Jade/Physics/Collision.hpp>
#include <memory>
#include <span>
#include <vector>

namespace Jade { class JobSystem; }

namespace Jade::Physics {
struct Config {
  Vec3 gravity{0, -9.81f, 0};
  double fixedStep = 1.0 / 120.0;
  unsigned maxSubsteps = 8;
  unsigned velocityIterations = 20;
  unsigned positionIterations = 4;
  float contactDistance = 0.005f;
  float penetrationSlop = 0.002f;
};
struct ContactPoint {
  Vec3 pointA, pointB;
  float separation = 0;
  float normalImpulse = 0;
  Vec3 frictionImpulse;
};
struct Manifold {
  Entity a, b;
  Vec3 normal; // Impulses push b in this direction and a in the opposite direction.
  std::vector<ContactPoint> points;
};
struct Statistics {
  std::size_t dynamicBodies = 0, staticBodies = 0, contacts = 0;
  std::size_t candidatePairs = 0, islands = 0;
  float maximumPenetration = 0;
  std::uint64_t steps = 0;
};
// Read-only geometric queries, including static/static pairs. No GPU dependency.
std::vector<Manifold> manifolds(const Scene& scene, float distance = 0);

class System final {
public:
  explicit System(Config config = {});
  System(JobSystem& jobs, Config config = {});
  ~System();
  System(const System&) = delete;
  System& operator=(const System&) = delete;
  const Config& config() const noexcept;
  void step(Scene& scene, float dt);
  void applyImpulse(Scene& scene, Entity entity, Vec3 impulse, Vec3 worldPoint);
  void reset() noexcept;
  const Statistics& statistics() const noexcept;
  std::span<const Manifold> contacts() const noexcept;
  // Current solver anchors and impulses, or fresh geometry after Scene edits.
  // Includes static/static pairs for inspection without stepping the simulation.
  std::vector<Manifold> contactSnapshot(const Scene& scene) const;
private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Jade::Physics
