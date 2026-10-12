#include "ShapeDistance.hpp"
#include <bit>

namespace Jade::Physics::Internal {
namespace {
using V = Vector;
using Detail::collisionDot;
using Detail::collisionLength;
V offset(const Detail::WorldCollider& shape,V direction) {
  if (shape.kind == Detail::CollisionKind::Sphere) {
    const auto length = collisionLength(direction); return length > 0 ? direction*(shape.radius/length) : V{shape.radius,0,0};
  }
  V result;
  for (unsigned k = 0; k < 3; ++k) result = result+shape.axes[k]*(collisionDot(shape.axes[k],direction) >= 0 ? shape.halfExtents[k] : -shape.halfExtents[k]);
  return result;
}
struct Simplex { std::array<V,4> points; unsigned count = 0; };
V closest(Simplex& simplex) {
  double best = std::numeric_limits<double>::infinity(); V selected; unsigned selectedMask = 0;
  for (unsigned mask = 1; mask < (1u<<simplex.count); ++mask) {
    std::array<unsigned,4> indices{}; unsigned size = 0;
    for (unsigned i = 0; i < simplex.count; ++i) if (mask&(1u<<i)) indices[size++] = i;
    const auto p = simplex.points[indices[0]]; std::array<V,3> edges{};
    const auto dimensions = size-1; double matrix[3][4]{};
    for (unsigned i = 0; i < dimensions; ++i) edges[i] = simplex.points[indices[i+1]]-p;
    for (unsigned i = 0; i < dimensions; ++i) {
      for (unsigned j = 0; j < dimensions; ++j) matrix[i][j] = collisionDot(edges[i],edges[j]);
      matrix[i][dimensions] = -collisionDot(edges[i],p);
    }
    bool valid = true;
    for (unsigned column = 0; column < dimensions; ++column) {
      unsigned pivot = column; for (unsigned row = column+1; row < dimensions; ++row) if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) pivot = row;
      if (std::abs(matrix[pivot][column]) < 1e-20) { valid = false; break; }
      for (unsigned j = column; j <= dimensions; ++j) std::swap(matrix[pivot][j],matrix[column][j]);
      const auto inverse = 1/matrix[column][column]; for (unsigned j = column; j <= dimensions; ++j) matrix[column][j] *= inverse;
      for (unsigned row = 0; row < dimensions; ++row) if (row != column) {
        const auto factor = matrix[row][column]; for (unsigned j = column; j <= dimensions; ++j) matrix[row][j] -= factor*matrix[column][j];
      }
    }
    if (!valid) continue;
    V candidate = p; double sum = 0;
    for (unsigned i = 0; i < dimensions; ++i) { const auto weight = matrix[i][dimensions]; if (weight < -1e-10) valid = false; sum += weight; candidate = candidate+edges[i]*weight; }
    if (!valid || sum > 1+1e-10) continue;
    const auto distance = collisionDot(candidate,candidate); if (distance < best) { best = distance; selected = candidate; selectedMask = mask; }
  }
  Simplex reduced;
  for (unsigned i = 0; i < simplex.count; ++i) if (selectedMask&(1u<<i)) reduced.points[reduced.count++] = simplex.points[i];
  simplex = reduced; return selected;
}
}
DistancePlane shapeDistance(const Detail::WorldCollider& a,const Detail::WorldCollider& b) {
  const auto delta = a.center-b.center;
  const double scale = std::max({a.size(),b.size(),collisionLength(delta),1e-12});
  const auto support = [&](V direction) { return (delta+offset(a,direction)-offset(b,-direction))*(1/scale); };
  Simplex simplex; simplex.points[0] = support(-delta); simplex.count = 1;
  V v = simplex.points[0]; DistancePlane certificate{0,{1,0,0}};
  for (unsigned iteration = 0; iteration < 32; ++iteration) {
    const auto length = collisionLength(v); if (length < 1e-12) return certificate;
    const auto axis = v*(1/length); const auto next = support(-v);
    const auto bound = collisionDot(next,axis);
    if (bound > certificate.distance/scale) certificate = {std::max(0.0,bound*scale),-axis};
    if (length-bound < 1e-10 || simplex.count == 4) return certificate;
    bool duplicate = false; for (unsigned i = 0; i < simplex.count; ++i) if (collisionLength(simplex.points[i]-next) < 1e-12) duplicate = true;
    if (duplicate) return certificate;
    simplex.points[simplex.count++] = next; v = closest(simplex);
  }
  return certificate;
}
} // namespace Jade::Physics::Internal
