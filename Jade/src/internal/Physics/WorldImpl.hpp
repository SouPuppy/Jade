#pragma once
#include <Jade/Physics/World.hpp>
#include <Jade/Core/JobSystem.hpp>
#include "SpatialIndex.hpp"
#include "BroadPhase.hpp"
#include "Solver/ContactSolver.hpp"
#include <atomic>
#include <chrono>
#include <numeric>
#include <unordered_map>

namespace Jade::Physics {
struct World::Impl {
  using V = Vector;
  struct Slot {
    BodyDescription description;
    Internal::Body prototype;
    std::uint32_t generation = 1;
    bool alive = false, edited = true, geometryEdited = true;
    Entity entity;
    V force, torque, persistentForce, persistentTorque;
    double quiet = 0;
    ForceState forces;
    bool hasTarget = false;
    V targetPosition;
    Quaternion targetRotation;
    double targetTime = 0;
  };
  struct StateStorage {
    std::array<std::vector<double>,3> position, velocity, angular, momentum;
    std::vector<Quaternion> rotation;
    std::vector<unsigned char> sleeping;
    void resize(std::size_t n) {
      for (auto* array : {&position,&velocity,&angular,&momentum}) for (auto& v : *array) v.resize(n);
      rotation.resize(n); sleeping.resize(n);
    }
    void reserve(std::size_t n) {
      for (auto* array : {&position,&velocity,&angular,&momentum}) for (auto& v : *array) v.reserve(n);
      rotation.reserve(n); sleeping.reserve(n);
    }
    static V get(const std::array<std::vector<double>,3>& a, std::size_t i) { return {a[0][i],a[1][i],a[2][i]}; }
    static void put(std::array<std::vector<double>,3>& a, std::size_t i, V p) { for (unsigned k = 0; k < 3; ++k) a[k][i] = p[k]; }
  } state;
  struct Pair {
    std::uint32_t a = 0, b = 0, generationA = 0, generationB = 0;
    std::array<Internal::Cached,4> anchors;
    unsigned count = 0;
    WorldContact contact;
  };
  struct FieldSlot { std::shared_ptr<const ForceField> field; std::uint32_t generation = 1; };
  const std::uint64_t identity;
  Config config;
  JobSystem* jobs = nullptr;
  bool stepping = false, wakeAll = false;
  double time = 0, previousDt = 0;
  Statistics stats;
  std::vector<Slot> slots;
  std::vector<std::uint32_t> freeSlots;
  std::vector<FieldSlot> fields;
  Integration integration = Integration::SemiImplicitEuler;
  std::vector<Pair> pairs, nextPairs;
  std::vector<WorldContact> contacts;
  mutable Detail::SpatialIndex staticTree, movingTree;
  mutable bool indexDirty = true;
  mutable std::vector<Detail::WorldCollider> shapes;
  mutable std::vector<std::size_t> statics, moving;
  // Persistent scratch storage, never published before a successful commit.
  std::vector<Internal::Body> work;
  std::vector<FieldBody> snapshot;
  std::vector<std::pair<std::uint64_t,std::size_t>> entityIndex;
  std::vector<FieldSample> samples;
  std::vector<V> velocityBefore;
  std::vector<V> contactImpulse, solverVelocity;
  unsigned solvePass = 0;
  std::vector<unsigned char> awake;
  std::vector<double> quiet;
  std::vector<std::size_t> parents;
  std::vector<std::size_t> islandParents;
  bool graphDirty = true;
  std::vector<Detail::ColliderPair> candidates;
  struct Batch { std::size_t a = 0,b = 0; SmallList<Internal::Constraint,4> constraints; };
  std::vector<Batch> batches;
  std::vector<std::vector<std::size_t>> colors;
  std::vector<std::vector<Detail::ColliderPair>> pairBuffers;
  explicit Impl(Config c);
  void writable() const { if (stepping) throw std::logic_error("Cannot mutate a World during its simulation step."); }
  Slot& require(BodyID id);
  const Slot& require(BodyID id) const;
  BodyID id(std::size_t i) const { return {identity,static_cast<std::uint32_t>(i),slots[i].generation}; }
  static std::uint64_t key(std::size_t a,std::size_t b) { return (static_cast<std::uint64_t>(a)<<32)|b; }
  Internal::Body makeBody(std::size_t i) const;
  static Internal::Body makeBody(const BodyDescription& d);
  void ensureIndex() const;
  void updateIndex();
  void parallel(std::size_t n, const JobSystem::IndexFunction& f, std::size_t grain = 0) {
    if (jobs && n >= config.parallelThreshold)
      jobs->wait(jobs->parallelFor(n,f,{JobCategory::Physics,{},grain}),WaitMode::Help);
    else for (std::size_t i = 0; i < n; ++i) f(i);
  }
  void sample(double sampleTime);
  void collectPairs();
  void collide(double dt, Statistics& result, bool continuousImpact = false);
  void solve(Statistics& result);
  void integrateOrientation(Internal::Body& b, double dt);
  void drift(double dt);
  double earliestToi(double remaining);
  void sleep(double dt, Statistics& result);
  void invalidate(std::size_t i, bool geometry);
};
namespace Internal {
void validateDescription(const BodyDescription& d);
void validateConfig(const Config& c);
struct Separation { double distance = 0; Vector normal; };
Separation separation(const Detail::WorldCollider& a,const Detail::WorldCollider& b);
std::optional<double> conservativeCast(const Body& a,const Body& b,double duration,unsigned iterations,double margin);
}
} // namespace Jade::Physics
