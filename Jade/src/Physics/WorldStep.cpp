#include "../internal/Physics/WorldImpl.hpp"
#include "../internal/Physics/Manifold.hpp"
#include <Jade/Core/TaskGraph.hpp>
#include <bit>

namespace Jade::Physics {
namespace {
using namespace Internal;
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
bool nonzero(Vector v) { return v.x != 0 || v.y != 0 || v.z != 0; }
std::size_t root(std::vector<std::size_t>& parents,std::size_t i) {
  while (parents[i] != i) { parents[i] = parents[parents[i]]; i = parents[i]; } return i;
}
void join(std::vector<std::size_t>& parents,std::size_t a,std::size_t b) {
  a = root(parents,a); b = root(parents,b); parents[std::max(a,b)] = std::min(a,b);
}
}
void World::Impl::ensureIndex() const {
  if (!indexDirty) return;
  shapes.resize(slots.size()); statics.clear(); moving.clear();
  for (std::size_t i = 0; i < slots.size(); ++i) if (slots[i].alive) {
    shapes[i] = makeBody(i).shape;
    const auto& d = slots[i].description; if (!d.enabled || d.shape.empty()) continue;
    (d.motion == MotionType::Static ? statics : moving).push_back(i);
  }
  staticTree.update(shapes,statics,config.contactDistance*2);
  movingTree.update(shapes,moving,config.contactDistance*2); indexDirty = false;
}
void World::Impl::updateIndex() {
  shapes.resize(work.size()); predicted.resize(work.size()); statics.clear(); moving.clear();
  parallel(work.size(),[&](auto i) { if (slots[i].alive) {
    auto& b = work[i]; Detail::updateBounds(b.shape); shapes[i] = b.shape; predicted[i] = b.shape.bounds;
    const auto translation = b.velocity*(config.fixedStep*2);
    const double radius = b.shape.kind == Detail::CollisionKind::Sphere ? b.shape.radius : collisionLength(b.shape.halfExtents);
    const double rotation = radius*std::min(2.0,Internal::angularSpeedBound(b)*config.fixedStep*2);
    for (unsigned k = 0; k < 3; ++k) { predicted[i].minimum[k] += std::min(0.0,translation[k])-rotation; predicted[i].maximum[k] += std::max(0.0,translation[k])+rotation; }
  } });
  for (std::size_t i = 0; i < slots.size(); ++i) {
    const auto& s = slots[i]; if (!s.alive || !s.description.enabled || s.description.shape.empty()) continue;
    (s.description.motion == MotionType::Static ? statics : moving).push_back(i);
  }
  // Independent trees may update together; neither is queried before completion.
  if (jobs && work.size() >= config.parallelThreshold) {
    TaskGraph g;
    g.add([&] { staticTree.update(shapes,statics,config.contactDistance*2); },{JobCategory::Physics});
    g.add([&] { movingTree.update(shapes,moving,config.contactDistance*2,predicted); },{JobCategory::Physics});
    jobs->wait(jobs->submit(g),WaitMode::Help);
  } else { staticTree.update(shapes,statics,config.contactDistance*2); movingTree.update(shapes,moving,config.contactDistance*2,predicted); }
  indexDirty = false;
}
void World::Impl::sample(double sampleTime) {
  snapshot.resize(work.size()); samples.resize(work.size()); entityIndex.clear();
  parallel(work.size(),[&](auto i) {
    const auto& b = work[i];
    snapshot[i] = {slots[i].entity,b.shape.center.vector(),b.velocity.vector(),
      slots[i].alive && slots[i].description.enabled && !slots[i].description.shape.empty() && slots[i].description.motion == MotionType::Dynamic
        ? slots[i].description.mass : 0,id(i),b.shape.center,b.velocity,b.orientation,b.angular,b.momentum()};
    if (!slots[i].alive) snapshot[i].id = {};
  });
  for (std::size_t i = 0; i < snapshot.size(); ++i) if (snapshot[i].entity.valid()) entityIndex.emplace_back(snapshot[i].entity.id(),i);
  std::sort(entityIndex.begin(),entityIndex.end());
  kernels.clear(); kernels.reserve(fields.size());
  for (const auto& field : fields) if (field.field) kernels.push_back(field.field->prepare(snapshot,entityIndex));
  parallel(work.size(),[&](auto i) {
    FieldSample sum;
    if (slots[i].alive && snapshot[i].mass > 0) {
      if (!awake[i] && !slots[i].edited && !wakeAll) { samples[i] = {slots[i].forces.fieldForce,slots[i].forces.fieldTorque}; return; }
      const FieldContext context{snapshot[i],snapshot,entityIndex,sampleTime};
      for (const auto& kernel : kernels) {
        const auto next = kernel(context); sum.force = sum.force+next.force; sum.torque = sum.torque+next.torque;
        (void)sum.force.vector(); (void)sum.torque.vector();
      }
      sum.force = sum.force+slots[i].force+slots[i].persistentForce;
      sum.torque = sum.torque+slots[i].torque+slots[i].persistentTorque;
      (void)sum.force.vector(); (void)sum.torque.vector();
    }
    samples[i] = sum;
  });
}
void World::Impl::collectPairs() {
  constexpr std::size_t blockSize = 64;
  const auto count = (moving.size()+blockSize-1)/blockSize;
  pairBuffers.resize(count); for (auto& buffer : pairBuffers) buffer.clear();
  const auto scan = [&](auto block) {
    auto& buffer = pairBuffers[block];
    for (auto k = block*blockSize; k < std::min(moving.size(),(block+1)*blockSize); ++k) {
      const auto a = moving[k]; if (!awake[a] && !slots[a].edited) continue;
      const auto& da = slots[a].description;
      const auto visit = [&](auto b) {
        if (a == b || (!work[a].inverseMass && !work[b].inverseMass)) return;
        const auto& db = slots[b].description;
        if (!(da.category&db.mask) || !(db.category&da.mask)) return;
        if (shapes[a].bounds.overlaps(shapes[b].bounds,config.contactDistance)) buffer.push_back({std::min<std::size_t>(a,b),std::max<std::size_t>(a,b)});
      };
      staticTree.query(shapes[a].bounds,config.contactDistance,visit);
      movingTree.query(shapes[a].bounds,config.contactDistance,visit);
    }
  };
  if (jobs && moving.size() >= config.parallelThreshold) jobs->wait(jobs->parallelFor(count,scan,{JobCategory::Physics,{},1}),WaitMode::Help);
  else for (std::size_t i = 0; i < count; ++i) scan(i);
  std::size_t total = 0; for (const auto& b : pairBuffers) total += b.size();
  candidates.clear(); candidates.reserve(total);
  for (const auto& b : pairBuffers) candidates.insert(candidates.end(),b.begin(),b.end());
  std::sort(candidates.begin(),candidates.end(),[](auto a,auto b) { return a.a == b.a ? a.b < b.b : a.a < b.a; });
  candidates.erase(std::unique(candidates.begin(),candidates.end(),[](auto a,auto b) { return a.a == b.a && a.b == b.b; }),candidates.end());
}
void World::Impl::collide(double dt,Statistics& result,bool continuousImpact) {
  auto begin = Clock::now(); updateIndex(); collectPairs(); result.candidatePairs += candidates.size(); result.timing.broadPhase += elapsed(begin);
  // Wake whole cached islands when an active body reaches a sleeping member.
  reached.assign(work.size(),0);
  for (const auto p : candidates) {
    if (work[p.a].inverseMass && awake[p.b]) reached[root(parents,p.a)] = 1;
    if (work[p.b].inverseMass && awake[p.a]) reached[root(parents,p.b)] = 1;
  }
  for (std::size_t i = 0; i < work.size(); ++i) if (work[i].inverseMass && !awake[i] && reached[root(parents,i)]) {
    awake[i] = 1; quiet[i] = 0; work[i].edited = true;
    work[i].velocity = work[i].velocity+samples[i].force*(work[i].inverseMass*dt);
    work[i].angular = work[i].angular+work[i].inertia(samples[i].torque*dt);
    velocityBefore[i] = work[i].velocity;
  }
  begin = Clock::now(); batches.resize(candidates.size());
  parallel(batches.size(),[&](auto i) {
    const auto p = candidates[i]; auto& batch = batches[i]; batch.a = p.a; batch.b = p.b;
    const auto k = key(p.a,p.b);
    const auto found = std::lower_bound(pairs.begin(),pairs.end(),k,[](const auto& old,auto value) { return key(old.a,old.b) < value; });
    std::span<const Cached> previous;
    if (found != pairs.end() && key(found->a,found->b) == k && found->generationA == slots[p.a].generation && found->generationB == slots[p.b].generation)
      previous = {found->anchors.data(),found->count};
    batch.constraints = buildConstraints(work[p.a],work[p.b],p.a,p.b,previous,config,static_cast<float>(dt),solvePass == 0 ? previousDt : 0);
    batch.cacheHit = !previous.empty() && !work[p.a].edited && !work[p.b].edited;
    if (continuousImpact) for (auto& c : batch.constraints) if (c.target < 0) c.target = 0;
  },32);
  result.timing.narrowPhase += elapsed(begin);
  for (const auto& batch : batches) if (batch.cacheHit) ++result.cacheHits;
  begin = Clock::now();
  for (const auto& batch : batches) if (!batch.constraints.empty() && work[batch.a].inverseMass && work[batch.b].inverseMass) join(parents,batch.a,batch.b);
  colorMasks.assign(work.size(),0); auto& masks = colorMasks;
  for (auto& color : colors) color.clear(); if (colors.size() < 65) colors.resize(65);
  for (std::size_t i = 0; i < batches.size(); ++i) {
    const auto& b = batches[i]; if (b.constraints.empty()) continue;
    const auto occupied = (work[b.a].inverseMass ? masks[b.a] : 0)|(work[b.b].inverseMass ? masks[b.b] : 0);
    const auto color = occupied == ~std::uint64_t{0} ? 64u : static_cast<unsigned>(std::countr_one(occupied));
    colors[color].push_back(i);
    if (color < 64) {
      if (work[b.a].inverseMass) masks[b.a] |= std::uint64_t{1}<<color;
      if (work[b.b].inverseMass) masks[b.b] |= std::uint64_t{1}<<color;
    }
  }
  result.colors = 0; for (const auto& color : colors) if (!color.empty()) ++result.colors;
  parallel(batches.size(),[&](auto i) { for (auto& c : batches[i].constraints) prepareConstraint(work,c); });
  result.timing.preparation += elapsed(begin);
  solve(result);
  ++solvePass;
}
void World::Impl::solve(Statistics& result) {
  const auto begin = Clock::now();
  solverVelocity.resize(work.size()); parallel(work.size(),[&](auto i) { solverVelocity[i] = work[i].velocity; });
  solverMomentum.resize(work.size());
  const auto run = [&](const auto& operation) {
    for (std::size_t color = 0; color < colors.size(); ++color) {
      const auto& group = colors[color];
      const auto job = [&](auto i) { for (auto& c : batches[group[i]].constraints) operation(c); };
      if (color == 64) for (std::size_t i = 0; i < group.size(); ++i) job(i);
      else parallel(group.size(),job,32);
    }
  };
  run([&](auto& c) { warmConstraint(work,c); });
  const auto backend = selectBackend(config.backend);
  if (jobs && batches.size() >= config.parallelThreshold) {
    TaskGraph graph; TaskGraph::TaskId previous; bool hasPrevious = false;
    const auto append = [&](unsigned operation,std::size_t color) {
      const auto& group = colors[color]; if (group.empty()) return;
      const auto count = operation == 1 && color != 64 ? (group.size()+3)/4 : group.size();
      const auto task = [&,operation,color](std::size_t index) {
        const auto& members = colors[color];
        if (operation == 1 && color != 64) {
          for (unsigned point = 0; point < 4; ++point) {
            std::array<Constraint*,4> lanes{}; bool any = false;
            for (unsigned lane = 0; lane < 4; ++lane) if (index*4+lane < members.size()) {
              auto& constraints = batches[members[index*4+lane]].constraints;
              if (point < constraints.size()) { lanes[lane] = &constraints[point]; any = true; }
            }
            if (any) solveVelocityPacket(work,lanes,backend);
          }
        } else for (auto& c : batches[members[index]].constraints) {
          if (operation == 0) warmConstraint(work,c);
          else if (operation == 1) solveVelocity(work,c);
          else solvePosition(work,c,config);
        }
      };
      TaskGraph::TaskId next;
      if (color == 64 || group.size() < config.parallelThreshold) next = graph.add([task,count] { for (std::size_t i = 0; i < count; ++i) task(i); },{JobCategory::Physics});
      else next = graph.addParallelFor(count,task,{JobCategory::Physics,{},operation == 1 ? 16u : 32u});
      if (hasPrevious) graph.depends(next,previous); previous = next; hasPrevious = true;
    };
    // Warm starting is already applied above; only iteration barriers enter this graph.
    for (unsigned iteration = 0; iteration < config.velocityIterations; ++iteration)
      for (std::size_t color = 0; color < colors.size(); ++color) append(1,color);
    const auto beforePosition = graph.addParallelFor(work.size(),[&](auto i) { solverMomentum[i] = work[i].momentum(); },{JobCategory::Physics});
    if (hasPrevious) graph.depends(beforePosition,previous); previous = beforePosition; hasPrevious = true;
    for (unsigned iteration = 0; iteration < config.positionIterations; ++iteration)
      for (std::size_t color = 0; color < colors.size(); ++color) append(2,color);
    const auto afterPosition = graph.addParallelFor(work.size(),[&](auto i) { if (work[i].inverseMass) work[i].angular = work[i].inertia(solverMomentum[i]); },{JobCategory::Physics});
    graph.depends(afterPosition,previous);
    jobs->wait(jobs->submit(graph),WaitMode::Help);
    parallel(work.size(),[&](auto i) { if (work[i].inverseMass) contactImpulse[i] = contactImpulse[i]+(work[i].velocity-solverVelocity[i])*slots[i].description.mass; });
    result.timing.solve += elapsed(begin); return;
  }
  for (unsigned iteration = 0; iteration < config.velocityIterations; ++iteration) {
    for (std::size_t color = 0; color < colors.size(); ++color) {
      const auto& group = colors[color];
      if (color == 64) { for (const auto index : group) for (auto& c : batches[index].constraints) solveVelocity(work,c); continue; }
      const auto packets = (group.size()+3)/4;
      parallel(packets,[&](auto packet) {
        for (unsigned point = 0; point < 4; ++point) {
          std::array<Constraint*,4> lanes{};
          for (unsigned lane = 0; lane < 4; ++lane) if (packet*4+lane < group.size()) {
            auto& contacts = batches[group[packet*4+lane]].constraints; if (point < contacts.size()) lanes[lane] = &contacts[point];
          }
          if (std::any_of(lanes.begin(),lanes.end(),[](auto c) { return c != nullptr; })) solveVelocityPacket(work,lanes,backend);
        }
      },8);
    }
  }
  for (std::size_t i = 0; i < work.size(); ++i) solverMomentum[i] = work[i].momentum();
  for (unsigned i = 0; i < config.positionIterations; ++i) run([&](auto& c) { solvePosition(work,c,config); });
  for (std::size_t i = 0; i < work.size(); ++i) if (work[i].inverseMass) work[i].angular = work[i].inertia(solverMomentum[i]);
  parallel(work.size(),[&](auto i) { if (work[i].inverseMass) contactImpulse[i] = contactImpulse[i]+(work[i].velocity-solverVelocity[i])*slots[i].description.mass; });
  result.timing.solve += elapsed(begin);
}
void World::Impl::integrateOrientation(Body& b,double dt) {
  Internal::integrateOrientation(b,dt);
}
void World::Impl::drift(double dt) {
  parallel(work.size(),[&](auto i) {
    if (!awake[i]) return;
    auto& b = work[i]; const auto& s = slots[i];
    const double h = s.hasTarget ? std::min(dt,s.targetTime) : dt;
    b.shape.center = b.shape.center+b.velocity*h; integrateOrientation(b,h);
    if (s.hasTarget && dt+1e-12 >= s.targetTime) {
      b.shape.center = s.targetPosition; b.orientation = s.targetRotation; b.velocity = {}; b.angular = {};
      b.shape.axes = {b.orientation.rotate({1,0,0}),b.orientation.rotate({0,1,0}),b.orientation.rotate({0,0,1})};
    }
  });
}
void World::Impl::sleep(double dt,Statistics& result) {
  const auto n = work.size(); stable.assign(n,1); hasContact.assign(n,0); hasIsland.assign(n,0);
  if (!config.sleeping) {
    // Keep island diagnostics without evaluating equilibrium/contact residuals.
    // Disabling sleep must not change the contact solution or retain quiet time.
    for (const auto& batch : batches) if (!batch.constraints.empty()) {
      if (work[batch.a].inverseMass) hasIsland[root(parents,batch.a)] = 1;
      if (work[batch.b].inverseMass) hasIsland[root(parents,batch.b)] = 1;
    }
    result.islands = std::count(hasIsland.begin(),hasIsland.end(),1);
    for (std::size_t i = 0; i < n; ++i) if (work[i].inverseMass) { quiet[i] = 0; ++result.activeBodies; }
    return;
  }
  bool fieldsAllow = true; for (const auto& f : fields) if (f.field && !f.field->permitsSleep()) fieldsAllow = false;
  for (const auto& batch : batches) for (const auto& c : batch.constraints) {
    const double separation = collisionDot((work[c.b].shape.center-work[c.a].shape.center)+work[c.b].offset(c.localB)-work[c.a].offset(c.localA),c.normal);
    for (const auto i : {c.a,c.b}) if (work[i].inverseMass) {
      hasContact[i] = 1; hasIsland[root(parents,i)] = 1;
      const auto other = i == c.a ? c.b : c.a;
      if (slots[other].description.motion == MotionType::Kinematic && (nonzero(work[other].velocity) || nonzero(work[other].angular))) stable[root(parents,i)] = 0;
      if (separation < -config.contactDistance || std::abs(collisionDot(relative(work[c.a],work[c.b],c.ra,c.rb),c.normal)) > config.sleepVelocity)
        stable[root(parents,i)] = 0;
    }
  }
  for (const auto& p : pairs) if (p.count && slots[p.a].alive && slots[p.b].alive && !awake[p.a] && !awake[p.b]) {
    if (work[p.a].inverseMass) { hasContact[p.a] = 1; hasIsland[root(parents,p.a)] = 1; }
    if (work[p.b].inverseMass) { hasContact[p.b] = 1; hasIsland[root(parents,p.b)] = 1; }
  }
  for (std::size_t i = 0; i < n; ++i) if (work[i].inverseMass) {
    const auto r = root(parents,i); const auto& d = slots[i].description;
    const double radius = work[i].shape.kind == Detail::CollisionKind::Sphere ? work[i].shape.radius : collisionLength(work[i].shape.halfExtents);
    const double speed = collisionLength(work[i].velocity)+collisionLength(work[i].angular)*radius;
    const auto contact = contactImpulse[i]*(1/dt);
    const auto residual = samples[i].force+contact;
    if (!config.sleeping || !d.allowSleep || !fieldsAllow || speed > config.sleepVelocity ||
        nonzero(slots[i].force) || nonzero(slots[i].torque) || nonzero(slots[i].persistentForce) || nonzero(slots[i].persistentTorque) ||
        (awake[i] && collisionLength(residual)/d.mass*dt > config.sleepVelocity)) stable[r] = 0;
    if (!hasContact[i] && collisionLength(samples[i].force)/d.mass*dt > config.sleepVelocity) stable[r] = 0;
  }
  islandQuiet.assign(n,std::numeric_limits<double>::infinity());
  for (std::size_t i = 0; i < n; ++i) if (work[i].inverseMass) {
    const auto r = root(parents,i); quiet[i] = stable[r] ? quiet[i]+dt : 0; islandQuiet[r] = std::min(islandQuiet[r],quiet[i]);
  }
  result.islands = std::count(hasIsland.begin(),hasIsland.end(),1);
  for (std::size_t i = 0; i < n; ++i) if (work[i].inverseMass) {
    if (islandQuiet[root(parents,i)] >= config.sleepTime) { awake[i] = 0; work[i].velocity = {}; work[i].angular = {}; }
    if (awake[i]) ++result.activeBodies; else ++result.sleepingBodies;
  }
}
void World::step(double dt) {
  auto& p = *m_Impl; p.writable();
  if (!std::isfinite(dt) || dt <= 0 || dt > std::numeric_limits<float>::max() || !std::isfinite(p.time+dt)) throw std::invalid_argument("Physics dt must be positive and finite.");
  if (p.config.sleeping && !p.dirty && !p.wakeAll && !p.graphDirty && p.wakeRegions.empty() &&
      p.stats.dynamicBodies == p.stats.sleepingBodies && !p.stats.kinematicBodies &&
      std::all_of(p.fields.begin(),p.fields.end(),[](const auto& field) { return !field.field || field.field->permitsSleep(); })) {
    const auto begin = Clock::now(); p.time += dt; ++p.stats.steps;
    p.stats.candidatePairs = p.stats.cacheHits = p.stats.toiEvents = p.stats.colors = 0;
    p.stats.timing = {}; p.stats.timing.total = elapsed(begin); return;
  }
  p.stepping = true;
  struct Guard { Impl& p; bool committed = false; ~Guard() { p.stepping = false; if (!committed) { p.indexDirty = true; p.staticTree.clear(); p.movingTree.clear(); } } } guard{p};
  const auto start = Clock::now(); Statistics result; result.steps = p.stats.steps+1;
  const auto n = p.slots.size(); p.work.resize(n); p.awake.resize(n); p.quiet.resize(n); p.parents.resize(n); p.velocityBefore.resize(n);
  p.contactImpulse.assign(n,{}); p.solvePass = 0;
  p.parallel(n,[&](auto i) {
    p.work[i] = p.slots[i].alive ? p.makeBody(i) : Body{};
    const auto& s = p.slots[i];
    p.awake[i] = s.alive && s.description.enabled && !s.description.shape.empty() && s.description.motion != MotionType::Static &&
      (!p.state.sleeping[i] || s.edited || p.wakeAll || !p.config.sleeping);
    p.quiet[i] = s.quiet; p.parents[i] = !p.graphDirty && p.islandParents.size() == n ? p.islandParents[i] : i;
  });
  if (p.graphDirty || p.islandParents.size() != n) for (const auto& pair : p.pairs) if (pair.count && p.slots[pair.a].alive && p.slots[pair.b].alive && p.slots[pair.a].description.enabled && p.slots[pair.b].description.enabled &&
      pair.generationA == p.slots[pair.a].generation && pair.generationB == p.slots[pair.b].generation && p.work[pair.a].inverseMass && p.work[pair.b].inverseMass)
    join(p.parents,pair.a,pair.b);
  p.wakeRoots.assign(n,0); auto& wakeRoots = p.wakeRoots;
  for (std::size_t i = 0; i < n; ++i) if (p.awake[i]) wakeRoots[root(p.parents,i)] = 1;
  for (const auto& pair : p.pairs) if (pair.count && pair.generationA == p.slots[pair.a].generation && pair.generationB == p.slots[pair.b].generation) {
    for (const auto i : {pair.a,pair.b}) {
      const auto other = i == pair.a ? pair.b : pair.a;
      if (p.work[i].inverseMass && (p.slots[other].edited || (p.slots[other].description.motion == MotionType::Kinematic && p.awake[other]))) wakeRoots[root(p.parents,i)] = 1;
    }
  }
  if (!p.wakeRegions.empty() && !p.wakeAll) {
    p.updateIndex();
    for (const auto& region : p.wakeRegions) p.movingTree.query(region,p.config.contactDistance,[&](auto i) {
      if (p.work[i].inverseMass && p.shapes[i].bounds.overlaps(region,p.config.contactDistance)) wakeRoots[root(p.parents,i)] = 1;
    });
  }
  for (std::size_t i = 0; i < n; ++i) if (p.work[i].inverseMass && wakeRoots[root(p.parents,i)]) {
    if (!p.awake[i]) p.quiet[i] = 0; p.awake[i] = 1;
  }
  auto begin = Clock::now(); p.sample(p.time); result.timing.fields = elapsed(begin);
  if (p.integration == Integration::VelocityVerlet) p.firstSamples = p.samples;
  begin = Clock::now();
  const double kick = p.integration == Integration::VelocityVerlet ? .5 : 1;
  p.parallel(n,[&](auto i) {
    auto& b = p.work[i]; if (!p.awake[i] || !b.inverseMass) return;
    b.velocity = (b.velocity+p.samples[i].force*(b.inverseMass*dt*kick))*(1/(1+p.slots[i].description.linearDamping*dt));
    b.angular = (b.angular+b.inertia(p.samples[i].torque*(dt*kick)))*(1/(1+p.slots[i].description.angularDamping*dt));
  });
  bool continuous = false;
  for (std::size_t i = 0; i < n; ++i) if (p.slots[i].alive && p.slots[i].description.enabled && !p.slots[i].description.shape.empty() && p.slots[i].description.motion != MotionType::Static && p.slots[i].description.collision == CollisionMode::Continuous) continuous = true;
  if (!continuous) p.drift(dt);
  else {
    double remaining = dt;
    // Resolve existing contacts before searching for new time-of-impact events.
    for (std::size_t i = 0; i < n; ++i) p.velocityBefore[i] = p.work[i].velocity;
    p.collide(dt,result,true);
    while (remaining > dt*1e-9) {
      const double next = p.earliestToi(remaining);
      p.drift(next); remaining -= next;
      if (remaining <= dt*1e-9) break;
      if (++result.toiEvents > p.config.maxToiEvents) throw std::runtime_error("CCD event budget exhausted.");
      p.collide(std::max(remaining,dt*1e-6),result,true);
    }
  }
  result.timing.integration = elapsed(begin)-result.timing.broadPhase-result.timing.narrowPhase-result.timing.preparation-result.timing.solve;
  if (p.integration == Integration::VelocityVerlet) {
    begin = Clock::now(); p.sample(p.time+dt); result.timing.fields += elapsed(begin);
    p.parallel(n,[&](auto i) { auto& b = p.work[i]; if (p.awake[i] && b.inverseMass) {
      b.velocity = b.velocity+p.samples[i].force*(b.inverseMass*dt*.5); b.angular = b.angular+b.inertia(p.samples[i].torque*(dt*.5));
    } p.samples[i].force = (p.samples[i].force+p.firstSamples[i].force)*.5; p.samples[i].torque = (p.samples[i].torque+p.firstSamples[i].torque)*.5; });
  }
  p.parallel(n,[&](auto i) { p.velocityBefore[i] = p.work[i].velocity; });
  p.collide(dt,result,continuous);
  p.sleep(dt,result);
  begin = Clock::now(); p.nextPairs.resize(p.batches.size());
  p.parallel(p.batches.size(),[&](auto i) {
    const auto& batch = p.batches[i]; auto& next = p.nextPairs[i]; next.count = 0; next.contact.count = 0;
    next.a = static_cast<std::uint32_t>(batch.a); next.b = static_cast<std::uint32_t>(batch.b);
    next.generationA = p.slots[batch.a].generation; next.generationB = p.slots[batch.b].generation;
    const auto& a = p.work[batch.a]; const auto& b = p.work[batch.b];
    next.contact.a = p.id(batch.a); next.contact.b = p.id(batch.b);
    for (const auto& c : batch.constraints) {
      if (!validAnchor(a,b,c.localA,c.localB,c.normal,p.config.contactDistance)) continue;
      const auto pa = a.shape.center+a.offset(c.localA),pb = b.shape.center+b.offset(c.localB);
      const auto friction = c.tangent1*c.tangentImpulse1+c.tangent2*c.tangentImpulse2;
      next.anchors[next.count] = {a.shape.entity,b.shape.entity,c.normal,c.localA,c.localB,friction,c.normalImpulse};
      next.contact.points[next.count++] = {pa,pb,friction,collisionDot((b.shape.center-a.shape.center)+b.offset(c.localB)-a.offset(c.localA),c.normal),c.normalImpulse};
      next.contact.normal = c.normal;
    }
    next.contact.count = next.count;
  });
  // Retain sleeping contacts; no integration or solver work is necessary for them.
  for (const auto& old : p.pairs) if (p.slots[old.a].alive && p.slots[old.b].alive && p.slots[old.a].description.enabled && p.slots[old.b].description.enabled && old.generationA == p.slots[old.a].generation && old.generationB == p.slots[old.b].generation &&
      !p.awake[old.a] && !p.awake[old.b] && !p.slots[old.a].edited && !p.slots[old.b].edited && old.count) {
    const auto k = Impl::key(old.a,old.b);
    const auto it = std::lower_bound(p.candidates.begin(),p.candidates.end(),k,[](auto c,auto value) { return Impl::key(c.a,c.b) < value; });
    if (it == p.candidates.end() || Impl::key(it->a,it->b) != k) p.nextPairs.push_back(old);
  }
  if (!p.batches.empty() && p.nextPairs.size() > p.batches.size())
    std::inplace_merge(p.nextPairs.begin(),p.nextPairs.begin()+p.batches.size(),p.nextPairs.end(),[](const auto& a,const auto& b) { return Impl::key(a.a,a.b) < Impl::key(b.a,b.b); });
  p.contactOffsets.resize(p.nextPairs.size()); std::size_t contactCount = 0;
  for (std::size_t i = 0; i < p.nextPairs.size(); ++i) {
    p.contactOffsets[i] = contactCount; if (p.nextPairs[i].count) ++contactCount; result.contacts += p.nextPairs[i].count;
  }
  auto& nextContacts = p.nextContacts; nextContacts.resize(contactCount);
  constexpr std::size_t publicationBlock = 64;
  p.penetrationBlocks.assign((p.nextPairs.size()+publicationBlock-1)/publicationBlock,0);
  p.parallel(p.penetrationBlocks.size(),[&](auto block) {
    double maximum = 0;
    for (std::size_t i = block*publicationBlock; i < std::min(p.nextPairs.size(),(block+1)*publicationBlock); ++i) {
      const auto& pair = p.nextPairs[i]; if (!pair.count) continue;
      nextContacts[p.contactOffsets[i]] = pair.contact;
      for (unsigned k = 0; k < pair.count; ++k) maximum = std::max(maximum,-pair.contact.points[k].separation);
    }
    p.penetrationBlocks[block] = maximum;
  },1);
  for (const auto maximum : p.penetrationBlocks) result.maximumPenetration = std::max(result.maximumPenetration,static_cast<float>(maximum));
  auto& forces = p.nextForces; forces.assign(n,{});
  p.parallel(n,[&](auto i) {
    if (!p.slots[i].alive) return;
    auto& b = p.work[i]; (void)b.shape.center.vector(); (void)b.velocity.vector(); (void)b.angular.vector();
    (void)b.shape.bounds.minimum.vector(); (void)b.shape.bounds.maximum.vector();
    if (b.inverseMass) {
      if (!p.awake[i] && p.state.sleeping[i] && !p.wakeAll && !p.slots[i].edited) { forces[i] = p.slots[i].forces; forces[i].status = ForceStatus::SleepingEquilibrium; }
      else {
        const auto contact = p.contactImpulse[i]*(1/dt);
        forces[i] = {p.samples[i].force,p.samples[i].torque,contact,p.samples[i].force+contact,
          p.awake[i] ? ForceStatus::Solved : ForceStatus::SleepingEquilibrium};
      }
      (void)forces[i].totalForce.vector(); const auto momentum = b.momentum();
      for (unsigned k = 0; k < 3; ++k) if (!std::isfinite(momentum[k])) throw std::invalid_argument("Nonfinite angular momentum.");
    }
  });
  bool splitRequired = false;
  for (const auto& old : p.pairs) if (old.count && p.work[old.a].inverseMass && p.work[old.b].inverseMass) {
    const auto k = Impl::key(old.a,old.b);
    const auto it = std::lower_bound(p.nextPairs.begin(),p.nextPairs.end(),k,[](const auto& pair,auto value) { return Impl::key(pair.a,pair.b) < value; });
    if (it == p.nextPairs.end() || Impl::key(it->a,it->b) != k || !it->count) { splitRequired = true; break; }
  }
  p.islandParents.resize(n); // allocate before the commit boundary
  // No throwing operations beyond this point. Publish all runtime state together.
  for (std::size_t i = 0; i < n; ++i) if (p.slots[i].alive) {
    auto& s = p.slots[i]; auto& b = p.work[i];
    Impl::StateStorage::put(p.state.position,i,b.shape.center); Impl::StateStorage::put(p.state.velocity,i,b.velocity);
    Impl::StateStorage::put(p.state.angular,i,b.angular); Impl::StateStorage::put(p.state.momentum,i,b.momentum());
    p.state.rotation[i] = b.orientation; p.state.sleeping[i] = b.inverseMass && !p.awake[i];
    s.quiet = p.quiet[i]; s.edited = false; s.geometryEdited = false; s.force = {}; s.torque = {}; s.forces = forces[i];
    s.prototype = b;
    if (s.hasTarget) { s.targetTime -= dt; if (s.targetTime <= 1e-12) s.hasTarget = false; }
    if (s.description.enabled && !s.description.shape.empty()) {
      if (s.description.motion == MotionType::Dynamic) ++result.dynamicBodies;
      else if (s.description.motion == MotionType::Kinematic) ++result.kinematicBodies;
      else ++result.staticBodies;
    }
  }
  p.contacts.swap(nextContacts); p.pairs.swap(p.nextPairs); p.previousDt = dt; p.time += dt; p.wakeAll = false;
  p.wakeRegions.clear();
  p.dirty = false;
  std::copy(p.parents.begin(),p.parents.end(),p.islandParents.begin()); p.graphDirty = splitRequired;
  p.indexDirty = true; result.timing.publication = elapsed(begin); result.timing.total = elapsed(start); p.stats = result; guard.committed = true;
}
} // namespace Jade::Physics
