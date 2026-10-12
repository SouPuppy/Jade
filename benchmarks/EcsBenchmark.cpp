#ifdef JADE_ECS_BASELINE
#include "Registry.hpp"
#else
#include <Jade/ECS/Storage.hpp>
#endif
#include "AllocationCounter.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>

namespace {
struct Position { float x = 1, y = 2, z = 3; };
struct Velocity { float x = .1f, y = .2f, z = .3f; };
struct Visual { std::uint64_t resource = 42; };
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
template<class T> void add(Jade::Detail::Registry& registry,std::uint32_t id) {
#ifdef JADE_ECS_BASELINE
  registry.Add(id,typeid(T),std::make_shared<T>());
#else
  registry.Add<T>(id);
#endif
}
template<class T> T& get(Jade::Detail::Registry& registry,std::uint32_t id) {
#ifdef JADE_ECS_BASELINE
  return *static_cast<T*>(registry.Get(id,typeid(T)));
#else
  return registry.Get<T>(id);
#endif
}
}
int main() {
  std::cout<<"entities,create_ms,creation_allocations,requested_bytes,snapshot_100_ms,batch_100_ms,destroy_ms,checksum\n"<<std::fixed<<std::setprecision(3);
  for(const auto count:{400u,4096u,100000u}) {
    BenchmarkAllocation::start();const auto start=Clock::now();Jade::Detail::Registry registry;
    std::vector<std::pair<std::uint32_t,std::uint32_t>> entities;entities.reserve(count);
    for(unsigned i=0;i<count;++i) {
      const auto id=registry.Create();entities.push_back(id);
      add<Position>(registry,id.first);add<Velocity>(registry,id.first);add<Visual>(registry,id.first);
    }
    const auto creation=milliseconds(start);const auto allocations=BenchmarkAllocation::stop();
    double checksum=0;auto phase=Clock::now();
    for(int repeat=0;repeat<100;++repeat) for(auto [id,generation]:registry.Matching({typeid(Position),typeid(Velocity)})) {
      (void)generation;checksum+=get<Position>(registry,id).x+get<Velocity>(registry,id).x;
    }
    const auto snapshots=milliseconds(phase);phase=Clock::now();
    for(int repeat=0;repeat<100;++repeat) {
#ifdef JADE_ECS_BASELINE
      for(auto [id,generation]:registry.Matching({typeid(Position),typeid(Velocity)})) {
        (void)generation;get<Position>(registry,id).x+=get<Velocity>(registry,id).x;
      }
#else
      registry.Visit({typeid(Position),typeid(Velocity)},[&](auto id,auto) {get<Position>(registry,id).x+=get<Velocity>(registry,id).x;});
#endif
    }
    const auto batches=milliseconds(phase);phase=Clock::now();
    for(auto [id,generation]:entities) registry.Destroy(id,generation);
    std::cout<<count<<','<<creation<<','<<allocations.calls<<','<<allocations.bytes<<','<<snapshots<<','<<batches<<','<<milliseconds(phase)<<','<<checksum<<'\n';
  }
}
