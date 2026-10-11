#include <Jade/Physics/World.hpp>
#include <Jade/Core/JobSystem.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

using namespace Jade;
using namespace Jade::Physics;
int main(int argc,char** argv) {
  try {
    unsigned workers = 3,steps = 32,warmup = 4; bool continuous = false; std::size_t count = 10000; std::string scene = "sparse",output;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i]; if (i+1 == argc) throw std::invalid_argument("Missing option value.");
      const std::string value = argv[++i];
      if (arg == "--workers") workers = static_cast<unsigned>(std::stoul(value));
      else if (arg == "--count") count = std::stoull(value);
      else if (arg == "--steps") steps = static_cast<unsigned>(std::stoul(value));
      else if (arg == "--warmup") warmup = static_cast<unsigned>(std::stoul(value));
      else if (arg == "--continuous") continuous = std::stoul(value) != 0;
      else if (arg == "--scene") scene = value;
      else if (arg == "--output") output = value;
      else throw std::invalid_argument("Unknown benchmark option.");
    }
    if (!steps || !count || count > 1000000 || workers > 256) throw std::invalid_argument("Invalid benchmark size.");
    if (scene != "sparse" && scene != "dense" && scene != "rest" && scene != "scene5" && scene != "fields" && scene != "mixed" && scene != "ccd")
      throw std::invalid_argument("Unknown benchmark scene.");
    std::unique_ptr<JobSystem> jobs; if (workers) jobs = std::make_unique<JobSystem>(JobSystemConfig{workers});
    Config config; config.sleeping = scene == "rest"; World world(config); world.setJobSystem(jobs.get()); world.reserve(count+2);
    if (scene == "dense" || scene == "rest" || scene == "scene5") {
      BodyDescription floor; floor.motion = MotionType::Static;
      floor.shape = Collider{Box{{scene == "scene5" ? 70.0f : 1000.0f,.5f,1000}}}; floor.position = {0,-.5,scene == "scene5" ? -54.0 : 0}; world.create(floor);
    }
    std::vector<BodyID> sources;
    for (std::size_t i = 0; i < count; ++i) {
      BodyDescription d;
      if (scene == "scene5") d.position = {(double(i%100)-49.5)*1.04,double(i/100%100)+.5,-double(i/10000)*12};
      else if (scene == "dense" || scene == "rest") d.position = {double(i%100),.5,double(i/100)};
      else d.position = {double(i%100)*20,double(i/100%100)*20,double(i/10000)*20};
      if (scene == "mixed") { const float s = i%3 == 0 ? .1f : i%3 == 1 ? 1.0f : 3.0f; d.shape = Collider{Box{{s,s,s}}}; d.mass = i%2 ? .1 : 1000; }
      if (scene == "sparse" || scene == "mixed") { d.velocity = {double(i%7)*.1,0,double(i%11)*.1}; d.angularVelocity = {0,.1,0}; }
      if (scene == "fields") d.shape = Collider{Sphere{.5f}};
      if (scene == "ccd") { d.collision = CollisionMode::Continuous; d.velocity = {1,0,0}; }
      const auto id = world.create(d); if (i < 64) sources.push_back(id);
    }
    if (scene == "scene5") {
      BodyDescription ball; ball.shape = Collider{Sphere{3}}; ball.position = {0,50,10}; ball.mass = 3500; ball.velocity = {0,0,-60}; ball.collision = continuous ? CollisionMode::Continuous : CollisionMode::Discrete; world.create(ball);
    }
    if (scene == "fields") world.setFields({std::make_shared<BodyGravity>(sources,1,.1)});
    for (unsigned i = 0; i < warmup; ++i) world.step(1.0/120);
    const auto before = jobs ? jobs->statistics() : JobStatistics{};
    std::vector<double> times; StageTimes sum; const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < steps; ++i) {
      world.step(1.0/120); const auto t = world.statistics().timing; times.push_back(t.total);
      sum.fields += t.fields; sum.integration += t.integration; sum.broadPhase += t.broadPhase; sum.narrowPhase += t.narrowPhase;
      sum.preparation += t.preparation; sum.solve += t.solve; sum.publication += t.publication;
    }
    const double wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
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
    std::ostringstream row;
    row << std::fixed << std::setprecision(4) << scene << ',' << count << ',' << workers << ',' << steps << ',' << percentile(.5) << ',' << percentile(.95)
        << ',' << sum.fields/steps << ',' << sum.integration/steps << ',' << sum.broadPhase/steps << ',' << sum.narrowPhase/steps << ',' << sum.preparation/steps
        << ',' << sum.solve/steps << ',' << sum.publication/steps << ',' << stats.candidatePairs << ',' << stats.contacts << ',' << stats.islands << ',' << stats.colors
        << ',' << stats.activeBodies << ',' << stats.sleepingBodies << ',' << participating << ',' << busy/1e9/wallSeconds << '\n';
    constexpr auto header = "scene,count,workers,steps,p50_ms,p95_ms,fields_ms,integration_ms,broad_ms,narrow_ms,prepare_ms,solve_ms,publish_ms,candidates,contacts,islands,colors,active,sleeping,participating,effective_worker_cores\n";
    std::cout << header << row.str();
    if (!output.empty()) { std::ofstream file(output); if (!file) throw std::runtime_error("Cannot write benchmark output."); file << header << row.str(); }
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
