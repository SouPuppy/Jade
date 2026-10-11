#pragma once

#include <Jade/Core/Entity.hpp>
#include <Jade/Math/Vector.hpp>
#include <span>
#include <Jade/Physics/Types.hpp>
#include <utility>

namespace Jade {
class Scene;
}
namespace Jade::Physics {

// Immutable world-space state for one force sampling stage.
struct FieldBody {
  Entity entity;
  Vec3 position, velocity;
  double mass = 0;
  BodyID id;
  Vector precisePosition, preciseVelocity;
};
struct FieldSample { Vector force, torque; };
struct FieldContext {
  const FieldBody &body;
  std::span<const FieldBody> bodies;
  std::span<const std::pair<std::uint64_t,std::size_t>> entityIndex;
  double time = 0;
  const FieldBody* find(Entity entity) const;
  const FieldBody* find(BodyID id) const;
};

// Center-of-mass force in N. Implementations must be immutable and safe for
// concurrent sampling. Sampling must not access or mutate live Scene state.
class ForceField {
public:
  virtual ~ForceField() = default;
  virtual Vec3 sample(const FieldContext &context) const = 0;
  virtual FieldSample sampleStage(const FieldContext& context) const {
    const auto p = sample(context); return {{p.x,p.y,p.z},{}};
  }
  // Unknown/time-dependent fields cannot certify a stationary equilibrium.
  virtual bool permitsSleep() const noexcept { return false; }
  // Runs on the owning thread before sampling; source-dependent fields can
  // reject foreign or malformed sources here. No Scene changes are allowed.
  virtual void validate(const Scene &) const {}
};

} // namespace Jade::Physics
