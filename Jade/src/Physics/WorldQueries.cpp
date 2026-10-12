#include "../internal/Physics/WorldImpl.hpp"
#include "../internal/Physics/Manifold.hpp"
#include "../internal/Physics/ShapeDistance.hpp"
#include <sstream>

namespace Jade::Physics {
namespace Internal {
namespace {
using V = Vector;
double projectedRadius(const Detail::WorldCollider& b,V axis) {
  if (b.kind == Detail::CollisionKind::Sphere) return b.radius;
  double r = 0; for (unsigned k = 0; k < 3; ++k) r += b.halfExtents[k]*std::abs(collisionDot(b.axes[k],axis)); return r;
}
double projectedSweep(const Body& b,V axis,double h) {
  if (b.shape.kind == Detail::CollisionKind::Sphere) return b.shape.radius;
  double radius = 0,extent = 0;
  for (unsigned k = 0; k < 3; ++k) {
    const auto direction = b.shape.axes[k];
    radius += b.shape.halfExtents[k]*std::abs(collisionDot(direction,axis)+collisionDot(collisionCross(b.angular,direction),axis)*h);
    extent += b.shape.halfExtents[k];
  }
  const auto omega = angularSpeedBound(b);
  double acceleration = 0;
  const auto momentum = b.momentum();
  if (b.inverseMass && collisionLength(collisionCross(momentum,b.angular)) > 1e-12*collisionLength(momentum)*collisionLength(b.angular))
    acceleration = std::max({b.inverseInertia.x,b.inverseInertia.y,b.inverseInertia.z})*collisionLength(momentum)*omega;
  // Taylor's remainder bounds every rotated support vertex. The absolute linear
  // terms also cover axis sign changes, without assuming a fixed support feature.
  return radius+.5*extent*(omega*omega+acceleration)*h*h;
}
struct SphereBoxSweep {
  V local,derivative,secondDerivative,extents;
  double radius = 0,omega = 0,acceleration = 0,distance = 0,speed = 0;
  bool constantAngular = true;
  SphereBoxSweep(const Body& sphere,const Body& box) {
    const auto delta = sphere.shape.center-box.shape.center,velocity = sphere.velocity-box.velocity;
    omega = angularSpeedBound(box); const auto momentum = box.momentum();
    if (box.inverseMass && collisionLength(collisionCross(momentum,box.angular)) > 1e-12*collisionLength(momentum)*collisionLength(box.angular)) {
      constantAngular = false;
      acceleration = std::max({box.inverseInertia.x,box.inverseInertia.y,box.inverseInertia.z})*collisionLength(momentum)*omega;
    }
    radius = sphere.shape.radius; extents = box.shape.halfExtents; distance = collisionLength(delta); speed = collisionLength(velocity);
    for (unsigned k = 0; k < 3; ++k) {
      local[k] = collisionDot(box.shape.axes[k],delta);
      derivative[k] = collisionDot(box.shape.axes[k],velocity)+collisionDot(collisionCross(box.angular,box.shape.axes[k]),delta);
      secondDerivative[k] = collisionDot(collisionCross(box.angular,collisionCross(box.angular,box.shape.axes[k])),delta)
        +2*collisionDot(collisionCross(box.angular,box.shape.axes[k]),velocity);
    }
  }
  double lowerDistance(double h) const {
    const double remainder = constantAngular ? (omega*omega*omega*(distance+speed*h)+3*omega*omega*speed)*h*h*h/6
      : .5*((omega*omega+acceleration)*(distance+speed*h)+2*omega*speed)*h*h;
    const auto value = [&](unsigned k,double t) { return local[k]+derivative[k]*t+(constantAngular ? .5*secondDerivative[k]*t*t : 0); };
    const auto quadraticRoots = [](double a,double b,double c,const auto& accept) {
      if (a == 0) { if (b != 0) accept(-c/b); return; }
      const auto discriminant = b*b-4*a*c; if (discriminant < 0) return;
      const auto q = -.5*(b+std::copysign(std::sqrt(discriminant),b));
      if (q == 0) accept(-b/(2*a)); else { accept(q/a); accept(c/q); }
    };
    SmallList<double,16> cuts{0,h};
    // Changes between a face, edge and corner occur only at these polynomial
    // coordinate crossings. Keeping the common time parameter avoids losing
    // cancellation between approaching and receding coordinates during grazing.
    for (unsigned k = 0; k < 3; ++k) for (const double side : {-1.0,1.0})
      quadraticRoots(constantAngular ? .5*secondDerivative[k] : 0,derivative[k],local[k]-side*extents[k],
        [&](double t) { if (t > 0 && t < h) cuts.push_back(t); });
    std::sort(cuts.begin(),cuts.end());
    const auto squaredDistance = [&](double t) {
      double result = 0; for (unsigned k = 0; k < 3; ++k) { const auto d = std::max(0.0,std::abs(value(k,t))-extents[k]); result += d*d; } return result;
    };
    double best = std::min(squaredDistance(0),squaredDistance(h));
    for (std::size_t interval = 1; interval < cuts.size(); ++interval) {
      const double start = cuts[interval-1],end = cuts[interval]; best = std::min(best,squaredDistance(end));
      if (!(end > start)) continue;
      std::array<double,4> d{};
      for (unsigned k = 0; k < 3; ++k) {
        const auto middle = value(k,(start+end)*.5); if (std::abs(middle) <= extents[k]) continue;
        const auto sign = middle < 0 ? -1.0 : 1.0;
        const auto a = sign*local[k]-extents[k],b = sign*derivative[k],c = constantAngular ? .5*sign*secondDerivative[k] : 0;
        d[0] += a*b; d[1] += b*b+2*a*c; d[2] += 3*b*c; d[3] += 2*c*c;
      }
      if (d[3] == 0 && d[2] == 0) {
        const double critical = d[1] != 0 ? -d[0]/d[1] : start;
        if (critical > start && critical < end) best = std::min(best,squaredDistance(critical));
        continue;
      }
      SmallList<double,4> monotonic{start,end};
      quadraticRoots(3*d[3],2*d[2],d[1],[&](double t) { if (t > start && t < end) monotonic.push_back(t); });
      std::sort(monotonic.begin(),monotonic.end());
      const auto slope = [&](double t) { return ((d[3]*t+d[2])*t+d[1])*t+d[0]; };
      for (std::size_t part = 1; part < monotonic.size(); ++part) {
        double lo = monotonic[part-1],hi = monotonic[part]; const double first = slope(lo),last = slope(hi);
        if ((first < 0) == (last < 0) && first != 0 && last != 0) continue;
        for (unsigned split = 0; split < 32; ++split) { const auto middle = (lo+hi)*.5; if ((slope(middle) < 0) == (first < 0)) lo = middle; else hi = middle; }
        best = std::min(best,squaredDistance((lo+hi)*.5));
      }
    }
    // Distance to a convex box is 1-Lipschitz in its input point.
    return std::sqrt(best)-std::sqrt(3.0)*remainder-radius-1e-10;
  }
};
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
    const auto length = collisionLength(axis); if (length < 1e-12) return;
    axis = axis*(1/length); const auto c = collisionDot(delta,axis);
    const auto gap = std::abs(c)-projectedRadius(a,axis)-projectedRadius(b,axis);
    if (gap > result.distance) result = {gap,c >= 0 ? axis : -axis};
  };
  for (const auto axis : a.axes) test(axis); for (const auto axis : b.axes) test(axis);
  for (const auto axis : a.axes) for (const auto other : b.axes) test(collisionCross(axis,other));
  if (result.distance > 0) {
    const auto plane = shapeDistance(a,b); if (plane.distance > result.distance) result = {plane.distance,plane.normal};
  }
  return result;
}
std::optional<double> conservativeCast(const Body& initialA,const Body& initialB,double duration,unsigned iterations,double margin) {
  const auto first = separation(initialA.shape,initialB.shape); if (first.distance <= margin) return 0;
  const double radiusA = initialA.shape.kind == Detail::CollisionKind::Sphere ? initialA.shape.radius : collisionLength(initialA.shape.halfExtents);
  const double radiusB = initialB.shape.kind == Detail::CollisionKind::Sphere ? initialB.shape.radius : collisionLength(initialB.shape.halfExtents);
  const double angularA = angularSpeedBound(initialA)*radiusA;
  const double angularB = angularSpeedBound(initialB)*radiusB;
  const double speed = collisionLength(initialA.velocity-initialB.velocity)+angularA+angularB;
  if (!(speed > 0)) return std::nullopt;
  double t = 0,lastDistance = first.distance,lastPlane = first.distance,lastAdvance = 0;
  for (unsigned iteration = 0; iteration < iterations; ++iteration) {
    Body a = initialA,b = initialB; a.shape.center = a.shape.center+a.velocity*t; b.shape.center = b.shape.center+b.velocity*t;
    integrateOrientation(a,t); integrateOrientation(b,t);
    const auto gap = separation(a.shape,b.shape);
    lastDistance = gap.distance;
    // A positive TOI envelope permits early arrival without surface penetration.
    // Keep its convergence tolerance inside that envelope; zero-margin public
    // shape casts retain the stricter absolute tolerance.
    const double tolerance = std::max(1e-8,margin*.01);
    if (gap.distance <= margin+tolerance) return t;
    const bool mixed = a.shape.kind != b.shape.kind;
    std::optional<SphereBoxSweep> sphereBox;
    if (mixed) sphereBox.emplace(a.shape.kind == Detail::CollisionKind::Sphere ? a : b,a.shape.kind == Detail::CollisionKind::Box ? a : b);
    const auto lowerGap = [&](double h) {
      // Local coordinate intervals preserve the curved sphere/face distance.
      // A support plane alone must use a whole face's extreme corner velocity,
      // which stalls conservative advancement during rotational grazing.
      if (sphereBox) return sphereBox->lowerDistance(h)-margin;
      return collisionDot((b.shape.center-a.shape.center)+(b.velocity-a.velocity)*h,gap.normal)
        -projectedSweep(a,gap.normal,h)-projectedSweep(b,gap.normal,h)-margin;
    };
    // This lower bound is concave in time: positive endpoints certify the entire
    // interval. Near grazing motion, its curvature bound is substantially tighter
    // than treating the complete angular sweep as a constant closing velocity.
    const auto available = duration-t;
    lastPlane = lowerGap(0)+margin;
    if (lowerGap(available) > 0) return std::nullopt;
    double lo = 0,hi = available;
    for (unsigned split = 0; split < 28; ++split) {
      const double middle = (lo+hi)*.5;
      if (lowerGap(middle) > 0) lo = middle; else hi = middle;
    }
    const double advance = lo;
    lastAdvance = advance;
    if (!(advance > 0) || !std::isfinite(advance)) throw std::runtime_error("CCD conservative advancement failed.");
    t += advance;
  }
  std::ostringstream error; error << "CCD distance iteration budget exhausted: t=" << t << ", duration=" << duration
    << ", initial gap=" << first.distance << ", final gap/plane/advance=" << lastDistance << '/' << lastPlane << '/' << lastAdvance << ", angular bounds=" << angularA << ',' << angularB
    << ", centers=" << initialA.shape.center.x << ',' << initialA.shape.center.y << ',' << initialA.shape.center.z
    << " / " << initialB.shape.center.x << ',' << initialB.shape.center.y << ',' << initialB.shape.center.z;
  throw std::runtime_error(error.str());
}
}
double World::Impl::earliestToi(double remaining) {
  updateIndex();
  double maximumTravel = 0;
  auto& continuous = continuousBodies; continuous.clear();
  swept.resize(work.size());
  for (const auto i : moving) {
    const double radius = shapes[i].kind == Detail::CollisionKind::Sphere ? shapes[i].radius : Detail::collisionLength(shapes[i].halfExtents);
    const double rotation = radius*std::min(2.0,Internal::angularSpeedBound(work[i])*remaining);
    const auto translation = work[i].velocity*remaining; swept[i] = shapes[i].bounds;
    for (unsigned axis = 0; axis < 3; ++axis) { swept[i].minimum[axis] += std::min(0.0,translation[axis])-rotation; swept[i].maximum[axis] += std::max(0.0,translation[axis])+rotation; }
    if (awake[i]) maximumTravel = std::max(maximumTravel,Detail::collisionLength(translation)+rotation);
  }
  for (const auto i : moving) if (slots[i].description.collision == CollisionMode::Continuous) continuous.push_back(i);
  auto& results = toiResults; results.assign(continuous.size(),remaining);
  const auto cast = [&](auto k) {
    const auto a = continuous[k];
    const auto& da = slots[a].description;
    const auto bounds = swept[a]; const double expansion = maximumTravel+config.contactDistance;
    const auto visit = [&](auto b) {
      if (a == b || (!work[a].inverseMass && !work[b].inverseMass)) return;
      const auto& db = slots[b].description;
      if (da.collision != CollisionMode::Continuous && db.collision != CollisionMode::Continuous) return;
      if (!(da.category&db.mask) || !(db.category&da.mask)) return;
      if (!bounds.overlaps(db.motion == MotionType::Static ? shapes[b].bounds : swept[b],config.contactDistance)) return;
      // The contact solve preceding this search already handles the full contact
      // envelope. Recasting its near-touching pairs creates repeated zero-length
      // events when conservative rotation bounds exceed actual closing motion.
      if (Internal::separation(shapes[a],shapes[b]).distance <= config.contactDistance) return;
      const auto hit = Internal::conservativeCast(work[a],work[b],remaining,config.maxToiIterations,config.contactDistance*.9);
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
  std::erase_if(result,[&](auto id) { return !Detail::collideManifoldPrecise(b.shape,m_Impl->shapes[id.slot],0); }); return result;
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
      Detail::updateBounds(at);
      const auto separation = Internal::separation(at,b.shape);
      auto point = at.center;
      if (const auto contact = Detail::collideManifoldPrecise(at,b.shape,1e-7); contact && !contact->points.empty()) point = contact->points.front().pointB;
      const QueryHit candidate{id,point,-separation.normal,*hit,std::max(0.0,-separation.distance)};
      if (!filter.acceptHit || filter.acceptHit(candidate)) result = candidate;
    }
  }
  return result;
}
} // namespace Jade::Physics
