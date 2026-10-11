#include "../internal/Physics/WorldImpl.hpp"
#include "../internal/Physics/Manifold.hpp"

namespace Jade::Physics {
namespace Internal {
namespace {
using V = Vector;
double projectedRadius(const Detail::WorldCollider& b,V axis) {
  if (b.kind == Detail::CollisionKind::Sphere) return b.radius;
  double r = 0; for (unsigned k = 0; k < 3; ++k) r += b.halfExtents[k]*std::abs(collisionDot(b.axes[k],axis)); return r;
}
}
Separation separation(const Detail::WorldCollider& a,const Detail::WorldCollider& b) {
  const auto delta = b.center-a.center;
  if (a.kind == Detail::CollisionKind::Sphere && b.kind == Detail::CollisionKind::Sphere) {
    const double length = collisionLength(delta); return {length-a.radius-b.radius,length > 0 ? delta*(1/length) : V{1,0,0}};
  }
  if (a.kind != b.kind) {
    const bool reverse = a.kind == Detail::CollisionKind::Box; const auto& s = reverse ? b : a; const auto& box = reverse ? a : b;
    const auto d = s.center-box.center; V toward;
    double insideDistance = std::numeric_limits<double>::infinity(); V insideNormal;
    for (unsigned k = 0; k < 3; ++k) {
      const auto c = collisionDot(d,box.axes[k]); toward = toward+box.axes[k]*(std::clamp(c,-box.halfExtents[k],box.halfExtents[k])-c);
      const auto gap = box.halfExtents[k]-std::abs(c); if (gap < insideDistance) { insideDistance = gap; insideNormal = box.axes[k]*(c >= 0 ? -1 : 1); }
    }
    const double length = collisionLength(toward); const auto normal = length > 0 ? toward*(1/length) : insideNormal;
    return {length > 0 ? length-s.radius : -insideDistance-s.radius,reverse ? -normal : normal};
  }
  Separation result{-std::numeric_limits<double>::infinity(),{1,0,0}};
  const auto test = [&](V axis) {
    const auto length = collisionLength(axis); if (length < 1e-8) return;
    axis = axis*(1/length); const auto c = collisionDot(delta,axis);
    const auto gap = std::abs(c)-projectedRadius(a,axis)-projectedRadius(b,axis);
    if (gap > result.distance) result = {gap,c >= 0 ? axis : -axis};
  };
  for (const auto axis : a.axes) test(axis); for (const auto axis : b.axes) test(axis);
  for (const auto axis : a.axes) for (const auto other : b.axes) test(collisionCross(axis,other));
  return result;
}
std::optional<double> conservativeCast(const Body& initialA,const Body& initialB,double duration,unsigned iterations,double margin) {
  const auto first = separation(initialA.shape,initialB.shape); if (first.distance <= margin) return 0;
  const double radiusA = initialA.shape.kind == Detail::CollisionKind::Sphere ? initialA.shape.radius : collisionLength(initialA.shape.halfExtents);
  const double radiusB = initialB.shape.kind == Detail::CollisionKind::Sphere ? initialB.shape.radius : collisionLength(initialB.shape.halfExtents);
  const double angularA = initialA.shape.kind == Detail::CollisionKind::Sphere ? 0 : collisionLength(initialA.angular)*radiusA;
  const double angularB = initialB.shape.kind == Detail::CollisionKind::Sphere ? 0 : collisionLength(initialB.angular)*radiusB;
  const double speed = collisionLength(initialA.velocity-initialB.velocity)+angularA+angularB;
  if (!(speed > 0)) return std::nullopt;
  double t = 0;
  for (unsigned iteration = 0; iteration < iterations; ++iteration) {
    Body a = initialA,b = initialB; a.shape.center = a.shape.center+a.velocity*t; b.shape.center = b.shape.center+b.velocity*t;
    a.turn(a.angular*t); b.turn(b.angular*t);
    const auto gap = separation(a.shape,b.shape);
    const double tolerance = std::max(1e-8,margin*1e-4);
    if (gap.distance <= margin+tolerance) return t;
    const double closing = -collisionDot(b.velocity-a.velocity,gap.normal)+angularA+angularB;
    if (!(closing > 0)) return std::nullopt;
    const double advance = (gap.distance-margin)/closing;
    if (t+advance > duration) return std::nullopt;
    if (!(advance > 0) || !std::isfinite(advance)) throw std::runtime_error("CCD conservative advancement failed.");
    t += advance;
  }
  throw std::runtime_error("CCD distance iteration budget exhausted.");
}
}
double World::Impl::earliestToi(double remaining) {
  updateIndex();
  double maximumTravel = 0;
  std::vector<std::size_t> continuous;
  for (const auto i : moving) if (awake[i]) maximumTravel = std::max(maximumTravel,Detail::collisionLength(work[i].velocity)*remaining);
  for (const auto i : moving) if (slots[i].description.collision == CollisionMode::Continuous) continuous.push_back(i);
  std::vector<double> results(continuous.size(),remaining);
  const auto cast = [&](auto k) {
    const auto a = continuous[k];
    const auto& da = slots[a].description;
    auto bounds = shapes[a].bounds; const auto translation = work[a].velocity*remaining;
    for (unsigned axis = 0; axis < 3; ++axis) { bounds.minimum[axis] += std::min(0.0,translation[axis]); bounds.maximum[axis] += std::max(0.0,translation[axis]); }
    const double radius = shapes[a].kind == Detail::CollisionKind::Sphere ? shapes[a].radius : Detail::collisionLength(shapes[a].halfExtents);
    const double expansion = maximumTravel+radius*std::min(2.0,Detail::collisionLength(work[a].angular)*remaining)+config.contactDistance;
    const auto visit = [&](auto b) {
      if (a == b || (!work[a].inverseMass && !work[b].inverseMass)) return;
      const auto& db = slots[b].description;
      if (da.collision != CollisionMode::Continuous && db.collision != CollisionMode::Continuous) return;
      if (!(da.category&db.mask) || !(db.category&da.mask)) return;
      const auto hit = Internal::conservativeCast(work[a],work[b],remaining,config.maxToiIterations,config.contactDistance*.5);
      // Existing contacts were solved before the TOI search.
      if (hit && *hit > remaining*1e-10) results[k] = std::min(results[k],*hit);
    };
    staticTree.query(bounds,expansion,visit); movingTree.query(bounds,expansion,visit);
  };
  parallel(continuous.size(),cast,1);
  return results.empty() ? remaining : *std::min_element(results.begin(),results.end());
}
std::vector<BodyID> World::query(const Aabb& bounds,QueryFilter filter) const {
  auto& p = *m_Impl; p.writable();
  for (unsigned k = 0; k < 3; ++k) if (!std::isfinite(bounds.minimum[k]) || !std::isfinite(bounds.maximum[k]) || bounds.minimum[k] > bounds.maximum[k])
    throw std::invalid_argument("Invalid query bounds.");
  p.ensureIndex(); std::vector<BodyID> result; const Detail::CollisionAabb b{bounds.minimum,bounds.maximum};
  const auto visit = [&](auto i) {
    const auto& d = p.slots[i].description;
    if (p.id(i) != filter.ignored && (d.category&filter.mask) && (filter.motions&(1u<<static_cast<unsigned>(d.motion))) && p.shapes[i].bounds.overlaps(b,0)) result.push_back(p.id(i));
  };
  p.staticTree.query(b,0,visit); p.movingTree.query(b,0,visit);
  std::sort(result.begin(),result.end(),[](auto a,auto b) { return a.slot < b.slot; }); return result;
}
std::vector<BodyID> World::overlap(const BodyDescription& description,QueryFilter filter) const {
  Internal::validateDescription(description); if (description.shape.empty()) return {};
  const auto b = Impl::makeBody(description); auto result = query({b.shape.bounds.minimum,b.shape.bounds.maximum},filter);
  std::erase_if(result,[&](auto id) { return !Detail::collideManifold(b.shape,m_Impl->shapes[id.slot],0); }); return result;
}
std::optional<QueryHit> World::rayCast(Vector origin,Vector displacement,QueryFilter filter) const {
  (void)origin.vector(); (void)displacement.vector(); const auto end = origin+displacement;
  Aabb bounds; for (unsigned k = 0; k < 3; ++k) { bounds.minimum[k] = std::min(origin[k],end[k]); bounds.maximum[k] = std::max(origin[k],end[k]); }
  const auto candidates = query(bounds,filter); std::optional<QueryHit> result;
  for (const auto id : candidates) {
    const auto& b = m_Impl->shapes[id.slot]; double t = 0; Vector normal;
    if (b.kind == Detail::CollisionKind::Sphere) {
      const auto delta = origin-b.center; const auto aa = Detail::collisionDot(displacement,displacement);
      const auto bb = Detail::collisionDot(delta,displacement),cc = Detail::collisionDot(delta,delta)-b.radius*b.radius;
      if (cc > 0) { const auto disc = bb*bb-aa*cc; if (!(aa > 0) || disc < 0) continue; t = (-bb-std::sqrt(disc))/aa; }
      const auto r = origin+displacement*t-b.center; const auto length = Detail::collisionLength(r); normal = length > 0 ? r*(1/length) : Vector{1,0,0};
    } else {
      double exit = 1; bool miss = false;
      for (unsigned k = 0; k < 3; ++k) {
        const auto o = Detail::collisionDot(origin-b.center,b.axes[k]),d = Detail::collisionDot(displacement,b.axes[k]);
        if (d == 0) { if (std::abs(o) > b.halfExtents[k]) miss = true; continue; }
        double enter = (-b.halfExtents[k]-o)/d,leave = (b.halfExtents[k]-o)/d;
        const auto n = b.axes[k]*(d > 0 ? -1 : 1); if (enter > leave) std::swap(enter,leave);
        if (enter > t) { t = enter; normal = n; } exit = std::min(exit,leave);
      }
      if (miss || t > exit) continue;
    }
    const QueryHit hit{id,origin+displacement*t,normal,t};
    if (t >= 0 && t <= 1 && (!result || t < result->fraction) && (!filter.acceptHit || filter.acceptHit(hit))) result = hit;
  }
  return result;
}
std::optional<QueryHit> World::shapeCast(const BodyDescription& description,Vector displacement,QueryFilter filter) const {
  Internal::validateDescription(description); (void)displacement.vector(); if (description.shape.empty()) return {};
  auto a = Impl::makeBody(description); a.velocity = displacement; a.angular = {};
  auto bounds = a.shape.bounds;
  for (unsigned k = 0; k < 3; ++k) { bounds.minimum[k] += std::min(0.0,displacement[k]); bounds.maximum[k] += std::max(0.0,displacement[k]); }
  std::optional<QueryHit> result;
  for (const auto id : query({bounds.minimum,bounds.maximum},filter)) {
    auto b = m_Impl->makeBody(id.slot); b.velocity = {}; b.angular = {};
    const auto hit = Internal::conservativeCast(a,b,1,m_Impl->config.maxToiIterations,0);
    if (hit && (!result || *hit < result->fraction)) {
      auto at = a.shape; at.center = at.center+displacement**hit;
      const auto separation = Internal::separation(at,b.shape);
      const QueryHit candidate{id,at.center,-separation.normal,*hit};
      if (!filter.acceptHit || filter.acceptHit(candidate)) result = candidate;
    }
  }
  return result;
}
} // namespace Jade::Physics
