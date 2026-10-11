#pragma once

#include <Jade/Core/Entity.hpp>
#include <Jade/Math/Vector.hpp>
#include <span>

namespace Jade {
class Scene;
}
namespace Jade::Physics {

// Immutable world-space state for one force sampling stage.
struct FieldBody {
  Entity entity;
  Vec3 position, velocity;
  float mass = 0;
};
struct FieldContext {
  const FieldBody &body;
  std::span<const FieldBody> bodies;
};

// Center-of-mass force in N. Implementations must be immutable and safe for
// concurrent sampling. Sampling must not access or mutate live Scene state.
class ForceField {
public:
  virtual ~ForceField() = default;
  virtual Vec3 sample(const FieldContext &context) const = 0;
  // Runs on the owning thread before sampling; source-dependent fields can
  // reject foreign or malformed sources here. No Scene changes are allowed.
  virtual void validate(const Scene &) const {}
};

} // namespace Jade::Physics
