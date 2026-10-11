#pragma once

#include <Jade/Physics/Field/ForceField.hpp>
#include <memory>
#include <vector>

namespace Jade::Physics {

inline constexpr double gravitationalConstant = 6.67430e-11;

class GravityField : public ForceField {
public:
  virtual Vec3 acceleration(const FieldContext &context) const = 0; // m/s^2
  Vec3 sample(const FieldContext &context) const final;             // mass * acceleration
};

class UniformGravity final : public GravityField {
public:
  explicit UniformGravity(Vec3 acceleration = {0, -9.81f, 0});
  Vec3 acceleration(const FieldContext &) const override;

private:
  const Vec3 m_Acceleration;
};

class PointGravity final : public GravityField {
public:
  PointGravity(Vec3 center, double mass, double g = gravitationalConstant, double softening = 0.001);
  Vec3 acceleration(const FieldContext &context) const override;

private:
  const Vec3 m_Center;
  const double m_Mass, m_G, m_Softening;
};

class CompositeGravity final : public GravityField {
public:
  explicit CompositeGravity(std::vector<std::shared_ptr<const GravityField>> fields);
  Vec3 acceleration(const FieldContext &context) const override;
  void validate(const Scene &scene) const override;

private:
  const std::vector<std::shared_ptr<const GravityField>> m_Fields;
};

// Only explicitly listed dynamic bodies generate gravity. Their position and
// mass come from the stage snapshot, so source edits take effect next step.
class BodyGravity final : public GravityField {
public:
  explicit BodyGravity(std::vector<Entity> sources, double g = gravitationalConstant,
                       double softening = 0.001);
  Vec3 acceleration(const FieldContext &context) const override;
  void validate(const Scene &scene) const override;

private:
  const std::vector<Entity> m_Sources;
  const double m_G, m_Softening;
};

} // namespace Jade::Physics
