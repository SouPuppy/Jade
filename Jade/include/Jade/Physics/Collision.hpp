#pragma once

#include <Jade/Core/Entity.hpp>
#include <Jade/Math/Vector.hpp>

#include <optional>
#include <vector>

namespace Jade { class Scene; }

namespace Jade::Physics {

struct Contact {
  Entity a, b;
  // World-space midpoint of surface witnesses (representative of a face contact).
  Vec3 point;
  // Unit separation direction for b, including when one shape contains the other.
  Vec3 normal;
  float penetration = 0;
};

// Main-thread, window-independent queries. Fresh world shapes and AABBs are
// derived from current components and ancestors on every call. Touching counts.
// Box allows orthogonal nonuniform world scale; Sphere requires uniform scale.
// Shear, invalid dimensions/transforms and unrepresentable results throw invalid_argument.
// Missing Collider/Transform or an empty Collider returns no contact. Foreign/dead entities throw.
std::optional<Contact> contact(const Scene& scene, Entity a, Entity b);

// One contact per pair, ordered by ascending Entity IDs. Entities without a
// Transform or with empty Colliders are skipped. Does not mutate components or apply collision response.
std::vector<Contact> detect(const Scene& scene);

} // namespace Jade::Physics
