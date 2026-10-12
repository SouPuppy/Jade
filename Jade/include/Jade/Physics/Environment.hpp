#pragma once

#include <Jade/Physics/Field/GravityField.hpp>

namespace Jade::Physics {

enum class Integration { SemiImplicitEuler, VelocityVerlet };

// Scene-owned rules. Replace immutable fields to change the environment.
struct Environment {
  std::vector<std::shared_ptr<const ForceField>> fields{std::make_shared<const UniformGravity>()};
  Integration integration = Integration::SemiImplicitEuler;
  void validate(const Jade::World &scene) const;
};

} // namespace Jade::Physics
