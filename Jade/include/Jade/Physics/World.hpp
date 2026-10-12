#pragma once
#include <Jade/Physics/Types.hpp>
#include <Jade/Physics/Collider.hpp>
#include <Jade/Physics/Environment.hpp>
#include <Jade/Math/Quaternion.hpp>
#include <memory>
#include <array>
#include <optional>
#include <functional>
#include <span>
#include <vector>

namespace Jade { class JobSystem; }
namespace Jade::Physics {
class World;
struct BodyDescription {
  Collider shape{Box{}};
  MotionType motion = MotionType::Dynamic;
  Vector position, velocity, angularVelocity;
  Rotation rotation;
  double mass = 1, friction = 0.6, restitution = 0;
  double linearDamping = 0, angularDamping = 0;
  bool enabled = true, allowSleep = true;
  CollisionMode collision = CollisionMode::Discrete;
  std::uint32_t category = 1, mask = ~std::uint32_t{0};
};
struct BodyState {
  Vector position, velocity, angularVelocity, angularMomentum;
  Rotation rotation;
  bool enabled = false, sleeping = false;
};
struct WorldContactPoint {
  Vector pointA, pointB, frictionImpulse;
  double separation = 0, normalImpulse = 0;
};
struct WorldContact {
  BodyID a, b;
  Vector normal;
  std::array<WorldContactPoint, 4> points;
  unsigned count = 0;
};
enum class ForceStatus { Stale, Solved, SleepingEquilibrium };
struct ForceState {
  Vector fieldForce, fieldTorque, contactForce, totalForce;
  ForceStatus status = ForceStatus::Stale;
};
struct Aabb { Vector minimum, maximum; };
struct QueryHit {
  BodyID body;
  Vector point, normal;
  double fraction = 0;
  double penetration = 0; // positive for a shape cast starting inside the target
};
struct QueryFilter {
  std::uint32_t mask = ~std::uint32_t{0};
  BodyID ignored;
  unsigned motions = 7; // bit 0 static, bit 1 dynamic, bit 2 kinematic
  std::function<bool(const QueryHit&)> acceptHit;
  QueryFilter() = default;
  QueryFilter(std::uint32_t categories) : mask(categories) {}
};
class WorldSnapshot final {
public:
  WorldSnapshot() = default;
  BodyState state(BodyID body) const;
  ForceState forces(BodyID body) const;
  std::span<const WorldContact> contacts() const;
  std::vector<BodyID> query(const Aabb& bounds, QueryFilter filter = {}) const;
  std::vector<BodyID> overlap(const BodyDescription& shape, QueryFilter filter = {}) const;
  std::optional<QueryHit> rayCast(Vector origin, Vector displacement, QueryFilter filter = {}) const;
  std::optional<QueryHit> shapeCast(const BodyDescription& shape, Vector displacement, QueryFilter filter = {}) const;
private:
  friend class World;
  struct Impl;
  std::shared_ptr<const Impl> m_Impl;
};
// Single writer. No mutation during step or callbacks; read-only snapshots may be copied.
class World final {
public:
  explicit World(Config config = {});
  World(JobSystem& jobs, Config config = {});
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  void setJobSystem(JobSystem* jobs);
  void configure(Config config);
  const Config& config() const noexcept;
  void reserve(std::size_t bodies);
  BodyID create(const BodyDescription& description);
  void destroy(BodyID body);
  bool contains(BodyID body) const noexcept;
  BodyState state(BodyID body) const;
  BodyDescription description(BodyID body) const;
  void update(BodyID body, const BodyDescription& description);
  void setEnabled(BodyID body, bool enabled);
  void teleport(BodyID body, Vector position, Rotation rotation, bool clearVelocity = false);
  void setVelocity(BodyID body, Vector linear, Vector angular = {});
  void addForce(BodyID body, Vector force, Vector torque = {});
  void setPersistentForce(BodyID body, Vector force, Vector torque = {});
  void applyImpulse(BodyID body, Vector impulse, Vector worldPoint);
  void wake(BodyID body);
  void setKinematicTarget(BodyID body, Vector position, Rotation rotation, double duration);
  FieldID addField(std::shared_ptr<const ForceField> field);
  void removeField(FieldID field);
  void setFields(std::vector<std::shared_ptr<const ForceField>> fields);
  std::vector<std::shared_ptr<const ForceField>> fields() const;
  std::vector<FieldBody> fieldSnapshot() const;
  void setIntegration(Integration integration);
  void step(double dt);
  double time() const noexcept;
  const Statistics& statistics() const noexcept;
  // Owned container capacity, excluding external fields, ECS and scheduler memory.
  std::size_t memoryUsage() const noexcept;
  std::span<const WorldContact> contacts() const noexcept;
  ForceState forces(BodyID body) const;
  WorldSnapshot snapshot() const;
  std::vector<BodyID> query(const Aabb& bounds, QueryFilter filter = {}) const;
  std::vector<BodyID> overlap(const BodyDescription& shape, QueryFilter filter = {}) const;
  std::optional<QueryHit> rayCast(Vector origin, Vector displacement, QueryFilter filter = {}) const;
  std::optional<QueryHit> shapeCast(const BodyDescription& shape, Vector displacement,
                                  QueryFilter filter = {}) const;
  // Authoring identity for diagnostics and Entity-based field source lookup.
  // Integration/collision/solver do not access live ECS state through it.
  void bind(BodyID body, Entity entity);
private:
  friend class System;
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
} // namespace Jade::Physics
