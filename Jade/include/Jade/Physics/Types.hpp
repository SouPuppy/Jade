#pragma once
#include <Jade/Math/Vector.hpp>
#include <Jade/Math/Quaternion.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <cmath>
#include <stdexcept>

namespace Jade::Physics {
// SI units. Runtime calculations use double precision; Scene presentation uses Vec3.
struct Vector {
  double x = 0, y = 0, z = 0;
  double operator[](std::size_t i) const { return i == 0 ? x : i == 1 ? y : z; }
  double& operator[](std::size_t i) { return i == 0 ? x : i == 1 ? y : z; }
  Vec3 vector() const {
    constexpr double limit = std::numeric_limits<float>::max();
    for (const auto value : {x,y,z}) if (!std::isfinite(value) || std::abs(value) > limit)
      throw std::invalid_argument("Physics value exceeds the supported Scene floating-point range.");
    return {static_cast<float>(x),static_cast<float>(y),static_cast<float>(z)};
  }
  bool operator==(const Vector&) const = default;
};
inline Vector operator+(Vector a, Vector b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vector operator-(Vector a, Vector b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vector operator-(Vector a) { return {-a.x,-a.y,-a.z}; }
inline Vector operator*(Vector a, double s) { return {a.x*s,a.y*s,a.z*s}; }
struct Rotation {
  double w = 1, x = 0, y = 0, z = 0;
  Rotation() = default;
  Rotation(double w,double x,double y,double z) : w(w),x(x),y(y),z(z) {}
  Rotation(Quaternion q) : w(q.w),x(q.x),y(q.y),z(q.z) {}
  Rotation normalized() const {
    const auto n = std::hypot(std::hypot(w,x),std::hypot(y,z));
    if (!std::isfinite(n) || n == 0) throw std::invalid_argument("Physics orientation must be finite and nonzero.");
    return {w/n,x/n,y/n,z/n};
  }
  Rotation conjugate() const { return {w,-x,-y,-z}; }
  Vector rotate(Vector v) const {
    const Vector q{x,y,z};
    const auto cross = [](Vector a,Vector b) { return Vector{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; };
    const auto t = cross(q,v)*2; return v+t*w+cross(q,t);
  }
  operator Quaternion() const { return Quaternion{static_cast<float>(w),static_cast<float>(x),static_cast<float>(y),static_cast<float>(z)}.normalized(); }
  bool operator==(const Rotation&) const = default;
};
inline Rotation operator*(Rotation a,Rotation b) {
  return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
    a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
struct BodyID {
  std::uint64_t world = 0;
  std::uint32_t slot = std::numeric_limits<std::uint32_t>::max(), generation = 0;
  explicit operator bool() const noexcept { return world != 0; }
  bool operator==(const BodyID&) const = default;
};
struct FieldID {
  std::uint64_t world = 0;
  std::uint32_t slot = 0, generation = 0;
  bool operator==(const FieldID&) const = default;
};
enum class MotionType { Static, Dynamic, Kinematic };
enum class CollisionMode { Discrete, Continuous };
enum class SolverBackend { Automatic, Scalar, SSE2, AVX2 };
struct Config {
  double fixedStep = 1.0 / 120.0;
  unsigned maxSubsteps = 8, velocityIterations = 20, positionIterations = 4;
  float contactDistance = 0.005f, penetrationSlop = 0.002f;
  bool sleeping = true;
  double sleepVelocity = 0.03, sleepTime = 0.5;
  std::size_t parallelThreshold = 256;
  unsigned maxToiEvents = 32, maxToiIterations = 32;
  SolverBackend backend = SolverBackend::Automatic;
  bool operator==(const Config&) const = default;
};
struct StageTimes {
  double fields = 0, integration = 0, broadPhase = 0, narrowPhase = 0;
  double preparation = 0, solve = 0, publication = 0, total = 0; // milliseconds
};
struct Statistics {
  std::size_t dynamicBodies = 0, staticBodies = 0, kinematicBodies = 0;
  std::size_t contacts = 0, candidatePairs = 0, islands = 0, activeBodies = 0, sleepingBodies = 0;
  std::size_t colors = 0, cacheHits = 0, toiEvents = 0;
  float maximumPenetration = 0;
  std::uint64_t steps = 0;
  StageTimes timing;
};
} // namespace Jade::Physics
