#pragma once
#include <Jade/World/World.hpp>
#include <Jade/Physics/RigidBody.hpp>
#include <Jade/Physics/Collision.hpp>
#include <Jade/Physics/Types.hpp>
#include <Jade/Physics/SmallList.hpp>
#include <memory>
#include <span>
#include <vector>

namespace Jade { class JobSystem; }

namespace Jade::Physics {
struct ContactPoint {
  Vec3 pointA, pointB;
  float separation = 0;
  float normalImpulse = 0;
  Vec3 frictionImpulse;
};
struct Manifold {
  Entity a, b;
  Vec3 normal; // Impulses push b in this direction and a in the opposite direction.
  SmallList<ContactPoint, 8> points;
};
// Average forces over the last successful simulation step. Contact force is
// the net applied contact impulse / dt, excluding positional corrections.
struct BodyForceSnapshot {
  Entity entity;
  Vec3 position, fieldForce, contactForce, totalForce;
  bool solved = false;
};
// Read-only geometric queries, including static/static pairs, skipping empty Colliders. No GPU dependency.
std::vector<Manifold> manifolds(const Jade::World& scene, float distance = 0);

class System final {
public:
  explicit System(Config config = {});
  System(JobSystem& jobs, Config config = {});
  ~System();
  System(const System&) = delete;
  System& operator=(const System&) = delete;
  const Config& config() const noexcept;
  void step(Jade::World& scene, float dt);
  // Owner-thread bridge; World invokes these at each fixed gameplay boundary.
  void synchronize(Jade::World& scene);
  void advance(Jade::World& scene, double dt);
  void publish(Jade::World& scene);
  // Requires a live Transform/RigidBody and a nonempty Collider; otherwise throws invalid_argument.
  void applyImpulse(Jade::World& scene, Entity entity, Vec3 impulse, Vec3 worldPoint);
  void reset() noexcept;
  const Statistics& statistics() const noexcept;
  std::span<const Manifold> contacts() const noexcept;
  // Per-World solver anchors and impulses, or fresh geometry after component edits.
  // Includes static/static pairs for inspection without stepping the simulation.
  std::vector<Manifold> contactSnapshot(const Jade::World& scene) const;
  // Stale states return fresh field force with solved=false (no total force).
  std::vector<BodyForceSnapshot> forceSnapshot(const Jade::World& scene) const;
private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Jade::Physics
