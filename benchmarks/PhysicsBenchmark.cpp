#include <Jade/Physics/World.hpp>
#include <Jade/Core/JobSystem.hpp>
#include <Jade/Physics/System.hpp>
#include <Jade/World/World.hpp>
#include <Jade/Core/Transform.hpp>
#include "AllocationCounter.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace Jade;
using namespace Jade::Physics;
int main(int argc,char** argv) {
  try {
    unsigned workers = 3,steps = 32,warmup = 4,toiIterations = 32; bool continuous = false,ecs = false,allocations = false; std::optional<bool> sleeping;
    std::size_t count = 10000; std::string scene = "sparse",output; Config config; Integration integration = Integration::SemiImplicitEuler;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i]; if (i+1 == argc) throw std::invalid_argument("Missing option value.");
      const std::string value = argv[++i];
      if (arg == "--workers") workers = static_cast<unsigned>(std::stoul(value));
      else if (arg == "--count") count = std::stoull(value);
      else if (arg == "--steps") steps = static_cast<unsigned>(std::stoul(value));
      else if (arg == "--warmup") warmup = static_cast<unsigned>(std::stoul(value));
      else if (arg == "--continuous") continuous = std::stoul(value) != 0;
      else if (arg == "--bridge") ecs = std::stoul(value) != 0;
      else if (arg == "--allocations") allocations = std::stoul(value) != 0;
      else if (arg == "--sleep") sleeping = std::stoul(value) != 0;
      else if (arg == "--toi-iterations") toiIterations = static_cast<unsigned>(std::stoul(value));
      else if (arg == "--backend") {
        if (value == "scalar") config.backend = SolverBackend::Scalar;
        else if (value == "sse2") config.backend = SolverBackend::SSE2;
        else if (value == "avx2") config.backend = SolverBackend::AVX2;
        else if (value != "auto") throw std::invalid_argument("Unknown solver backend.");
      }
      else if (arg == "--integration") {
        if (value == "verlet") integration = Integration::VelocityVerlet;
        else if (value != "euler") throw std::invalid_argument("Unknown integration method.");
      }
      else if (arg == "--scene") scene = value;
      else if (arg == "--output") output = value;
      else throw std::invalid_argument("Unknown benchmark option.");
    }
    if (!steps || !count || count > 1000000 || workers > 256) throw std::invalid_argument("Invalid benchmark size.");
    if (scene != "sparse" && scene != "dense" && scene != "rest" && scene != "scene5" && scene != "fields" && scene != "mixed" && scene != "ccd")
      throw std::invalid_argument("Unknown benchmark scene.");
    std::unique_ptr<JobSystem> jobs; if (workers) jobs = std::make_unique<JobSystem>(JobSystemConfig{workers});
    config.sleeping = sleeping.value_or(scene == "rest"); config.maxToiIterations = toiIterations;
    std::unique_ptr<Jade::World> authoring; if (ecs) authoring = std::make_unique<Jade::World>();
    Physics::World native(config); auto& world = authoring ? authoring->physicsWorld() : native;
    world.setJobSystem(jobs.get()); world.reserve(count+2);
    auto bridge = jobs ? std::make_unique<Physics::System>(*jobs,config) : std::make_unique<Physics::System>(config);
    std::vector<Entity> entities,sourceEntities; std::vector<BodyID> bodies; std::vector<Vector> initial;
    const auto create = [&](const BodyDescription& d) -> BodyID {
      initial.push_back(d.position);
      if (!authoring) { const auto id = world.create(d); bodies.push_back(id); return id; }
      const auto e = authoring->create<Archetype<Transform,Collider,RigidBody>>(); entities.push_back(e);
      e.get<Transform>().position = d.position.vector(); e.get<Transform>().rotation = d.rotation; e.get<Collider>() = d.shape;
      auto& rb = e.get<RigidBody>(); rb.mass = static_cast<float>(d.mass); rb.friction = static_cast<float>(d.friction); rb.restitution = static_cast<float>(d.restitution);
      rb.motion = d.motion; rb.collision = d.collision; rb.velocity = d.velocity.vector(); rb.angularVelocity = d.angularVelocity.vector();
      return {};
    };
    if (scene == "dense" || scene == "rest" || scene == "scene5") {
      BodyDescription floor; floor.motion = MotionType::Static;
      floor.shape = Collider{Box{{scene == "scene5" ? 70.0f : 1000.0f,.5f,scene == "scene5" ? 80.0f : 1000.0f}}}; floor.position = {0,-.5,scene == "scene5" ? -54.0 : 0}; create(floor);
    }
    if (scene == "ccd") {
      BodyDescription wall; wall.motion = MotionType::Static; wall.shape = Collider{Box{{.01f,1000,10000}}}; wall.restitution = 1;
      wall.position = {10,100,1000}; create(wall); wall.position.x = -10; create(wall);
    }
    std::vector<BodyID> sources;
    for (std::size_t i = 0; i < count; ++i) {
      BodyDescription d;
      if (scene == "scene5") { d.position = {double((static_cast<float>(i%100)-49.5f)*1.04f),double(i/100%100)+.5,-double(i/10000)*12}; d.friction = .6f; }
      else if (scene == "dense" || scene == "rest") d.position = {double(i%100),.5,double(i/100)};
      else d.position = {double(i%100)*20,double(i/100%100)*20,double(i/10000)*20};
      if (scene == "mixed") { const float s = i%3 == 0 ? .1f : i%3 == 1 ? 1.0f : 3.0f; d.shape = Collider{Box{{s,s,s}}}; d.mass = i%2 ? .1 : 1000; }
      if (scene == "sparse" || scene == "mixed") { d.velocity = {double(i%7)*.1,0,double(i%11)*.1}; d.angularVelocity = {0,.1,0}; }
      if (scene == "fields") d.shape = Collider{Sphere{.5f}};
      if (scene == "ccd") { d.shape = Collider{Sphere{.25f}}; d.position = {0,double(i%100)*2,double(i/100)*2}; d.collision = CollisionMode::Continuous; d.velocity = {1200,0,0}; d.restitution = 1; }
      const auto id = create(d); if (i < 64) { if (authoring) sourceEntities.push_back(entities.back()); else sources.push_back(id); }
    }
    if (scene == "scene5") {
      BodyDescription ball; ball.shape = Collider{Sphere{3}}; ball.position = {0,50,10}; ball.mass = 3500; ball.friction = .35f; ball.restitution = .1f; ball.velocity = {0,0,-60}; ball.collision = continuous ? CollisionMode::Continuous : CollisionMode::Discrete; create(ball);
    }
    if (authoring) { bridge->synchronize(*authoring); for (const auto e : sourceEntities) sources.push_back(authoring->physicsBody(e)); for (const auto e : entities) bodies.push_back(authoring->physicsBody(e)); }
    if (scene == "fields") world.setFields({std::make_shared<BodyGravity>(sources,1,.1)});
    if (scene == "ccd") world.setFields({});
    world.setIntegration(integration);
    const auto step = [&] { if (authoring) { bridge->synchronize(*authoring); bridge->advance(*authoring,1.0/120); bridge->publish(*authoring); } else world.step(1.0/120); };
    for (unsigned i = 0; i < warmup; ++i) step();
    const auto before = jobs ? jobs->statistics() : JobStatistics{};
    std::vector<double> times; times.reserve(steps); StageTimes sum; double bridgeTime = 0;
    if (allocations) BenchmarkAllocation::start();
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < steps; ++i) {
      const auto stepStart = std::chrono::steady_clock::now(); step(); const auto t = world.statistics().timing;
      const double total = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-stepStart).count(); times.push_back(total); bridgeTime += total-t.total;
      sum.fields += t.fields; sum.integration += t.integration; sum.broadPhase += t.broadPhase; sum.narrowPhase += t.narrowPhase;
      sum.preparation += t.preparation; sum.solve += t.solve; sum.publication += t.publication;
    }
    const double wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    const auto allocated = BenchmarkAllocation::stop();
    const auto after = jobs ? jobs->statistics() : JobStatistics{};
    std::sort(times.begin(),times.end()); const auto percentile = [&](double p) { return times[static_cast<std::size_t>((times.size()-1)*p)]; };
    std::uint64_t busy = 0; unsigned participating = 0;
    for (unsigned i = 0; i < after.workers.size(); ++i) {
      const auto dt = after.workers[i].categoryBusyNanoseconds[0]-before.workers[i].categoryBusyNanoseconds[0];
      busy += dt; if (dt) ++participating;
      std::cout << "worker=" << i << " physics_ms=" << dt/1e6 << " tasks=" << after.workers[i].tasks-before.workers[i].tasks
        << " lock_ms=" << (after.workers[i].lockWaitNanoseconds-before.workers[i].lockWaitNanoseconds)/1e6 << '\n';
    }
    const auto& stats = world.statistics();
    std::size_t moved = 0; double maximumSpeed = 0,maximumAngularSpeed = 0;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
      const auto state = world.state(bodies[i]); (void)state.position.vector(); (void)state.velocity.vector(); (void)state.angularVelocity.vector();
      if (std::hypot(state.position.x-initial[i].x,state.position.y-initial[i].y,state.position.z-initial[i].z) > .01) ++moved;
      maximumSpeed = std::max(maximumSpeed,std::hypot(state.velocity.x,state.velocity.y,state.velocity.z));
      maximumAngularSpeed = std::max(maximumAngularSpeed,std::hypot(state.angularVelocity.x,state.angularVelocity.y,state.angularVelocity.z));
    }
    std::ostringstream row;
    row << std::fixed << std::setprecision(4) << scene << ',' << count << ',' << workers << ',' << steps << ',' << percentile(.5) << ',' << percentile(.95)
        << ',' << sum.fields/steps << ',' << sum.integration/steps << ',' << sum.broadPhase/steps << ',' << sum.narrowPhase/steps << ',' << sum.preparation/steps
        << ',' << sum.solve/steps << ',' << sum.publication/steps << ',' << stats.candidatePairs << ',' << stats.contacts << ',' << stats.islands << ',' << stats.colors
        << ',' << stats.activeBodies << ',' << stats.sleepingBodies << ',' << participating << ',' << busy/1e9/wallSeconds
        << ',' << ecs << ',' << bridgeTime/steps << ',' << world.memoryUsage() << ',' << allocations << ',' << double(allocated.calls)/steps << ',' << double(allocated.bytes)/steps
        << ',' << config.sleeping << ',' << static_cast<unsigned>(integration) << ',' << static_cast<unsigned>(config.backend) << ',' << stats.toiEvents << ',' << stats.cacheHits
        << ',' << stats.maximumPenetration << ',' << moved << ',' << maximumSpeed << ',' << maximumAngularSpeed << '\n';
    constexpr auto header = "scene,count,workers,steps,p50_ms,p95_ms,fields_ms,integration_ms,broad_ms,narrow_ms,prepare_ms,solve_ms,publish_ms,candidates,contacts,islands,colors,active,sleeping,participating,effective_worker_cores,ecs_bridge,bridge_ms,core_capacity_bytes,allocation_counting,allocations_per_step,allocated_bytes_per_step,sleep_enabled,integration,backend,toi_events,cache_hits,max_penetration,moved_bodies,max_speed,max_angular_speed\n";
    std::cout << header << row.str();
    if (!output.empty()) { std::ofstream file(output); if (!file) throw std::runtime_error("Cannot write benchmark output."); file << header << row.str(); }
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
