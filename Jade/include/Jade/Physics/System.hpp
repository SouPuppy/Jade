#pragma once
#include <Jade/Core/Scene.hpp>
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
  SmallList<ContactPoint, 24> points;
};
// Average forces over the last successful simulation step. Contact force is
// the net applied contact impulse / dt, excluding positional corrections.
struct BodyForceSnapshot {
  Entity entity;
  Vec3 position, fieldForce, contactForce, totalForce;
  bool solved = false;
};
// Read-only geometric queries, including static/static pairs, skipping empty Colliders. No GPU dependency.
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
  // Frame-level bridge: synchronize/publish once around all Runtime substeps.
  void synchronize(Scene& scene);
  void advance(Scene& scene, double dt);
  void publish(Scene& scene);
  // Requires a live Transform/RigidBody and a nonempty Collider; otherwise throws invalid_argument.
  void applyImpulse(Scene& scene, Entity entity, Vec3 impulse, Vec3 worldPoint);
  void reset() noexcept;
  const Statistics& statistics() const noexcept;
  std::span<const Manifold> contacts() const noexcept;
  // Current solver anchors and impulses, or fresh geometry after Scene edits.
  // Includes static/static pairs for inspection without stepping the simulation.
  std::vector<Manifold> contactSnapshot(const Scene& scene) const;
  // Stale states return fresh field force with solved=false (no total force).
  std::vector<BodyForceSnapshot> forceSnapshot(const Scene& scene) const;
private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Jade::Physics
