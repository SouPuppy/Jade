#include <Jade/Physics/System.hpp>
#include <Jade/Core/Transform.hpp>
#include "../internal/Physics/Manifold.hpp"
#include "../internal/Physics/Solver/ContactSolver.hpp"
#include "../internal/Physics/BroadPhase.hpp"
#include <Jade/Core/TaskGraph.hpp>
#include <algorithm>
#include <map>
#include <numbers>

namespace Jade::Physics {
namespace {
using namespace Internal;
} // namespace

struct System::Impl {
  JobSystem* jobs = nullptr;
  Config config;
  Statistics stats;
  std::vector<Manifold> contacts;
  std::vector<Cached> cache;
  std::vector<Body> previous;
  std::vector<BodyForceSnapshot> forces;
  std::vector<std::shared_ptr<const ForceField>> fields;
  Integration integration = Integration::SemiImplicitEuler;
  double previousDt = 0;
  explicit Impl(Config c) : config(c) {}
};
System::System(Config config) : m_Impl(std::make_unique<Impl>(config)) {
  if (!std::isfinite(config.fixedStep) || config.fixedStep <= 0 || config.maxSubsteps == 0 ||
      config.velocityIterations == 0 || config.positionIterations == 0 ||
      !std::isfinite(config.contactDistance) || !std::isfinite(config.penetrationSlop) ||
      config.penetrationSlop < 0 || config.contactDistance < config.penetrationSlop)
    throw std::invalid_argument("Invalid Physics configuration.");
}
System::~System() = default;
System::System(JobSystem& jobs, Config config) : System(config) { m_Impl->jobs = &jobs; }
const Config& System::config() const noexcept { return m_Impl->config; }
const Statistics& System::statistics() const noexcept { return m_Impl->stats; }
std::span<const Manifold> System::contacts() const noexcept { return m_Impl->contacts; }
std::vector<BodyForceSnapshot> System::forceSnapshot(const Scene& scene) const {
  const auto& environment = scene.physics();
  environment.validate(scene);
  const auto shapes = Detail::captureColliders(scene);
  std::vector<Body> states; states.reserve(shapes.size());
  bool current = m_Impl->previousDt > 0 && shapes.size() == m_Impl->previous.size() &&
    environment.fields == m_Impl->fields && environment.integration == m_Impl->integration;
  for (std::size_t i = 0; i < shapes.size(); ++i) {
    states.push_back(body(scene, shapes[i]));
    if (current) {
      const auto& pose = shapes[i].entity.get<Transform>(); const auto& old = m_Impl->previous[i];
      current = pose.position == old.pose.position && pose.rotation == old.pose.rotation &&
        pose.scale == old.pose.scale && same(states.back(), old);
    }
  }
  if (current) return m_Impl->forces;
  const auto samples = sampleForces(environment, fieldBodies(states));
  std::vector<BodyForceSnapshot> result; result.reserve(states.size());
  for (std::size_t i = 0; i < states.size(); ++i) if (states[i].inverseMass > 0)
    result.push_back({states[i].shape.entity, states[i].shape.center.vector(), samples[i].vector()});
  return result;
}
std::vector<Manifold> System::contactSnapshot(const Scene& scene) const {
  const auto shapes = Detail::captureColliders(scene);
  bool current = m_Impl->previousDt > 0 && shapes.size() == m_Impl->previous.size();
  for (std::size_t i = 0; current && i < shapes.size(); ++i) {
    const auto& pose = shapes[i].entity.get<Transform>(); const auto& old = m_Impl->previous[i];
    current = pose.position == old.pose.position && pose.rotation == old.pose.rotation &&
      pose.scale == old.pose.scale && same(body(scene, shapes[i]), old);
  }
  if (!current) {
    std::vector<Manifold> result;
    std::vector<Body> states; states.reserve(shapes.size());
    for (const auto& shape : shapes) states.push_back(body(scene, shape));
    for (const auto pair : Detail::broadPhase(shapes, m_Impl->config.contactDistance)) {
      const auto& a = states[pair.a]; const auto& b = states[pair.b];
      const double margin = a.inverseMass || b.inverseMass ? m_Impl->config.contactDistance : 0;
      if (auto hit = Detail::collideManifold(a.shape, b.shape, margin)) {
        hit->points = persistentPoints(a, b, *hit, {}, margin);
        result.push_back(std::move(*hit));
      }
    }
    return result;
  }
  auto result = m_Impl->contacts;
  // The solver skips static/static pairs; the debug view still shows them.
  for (const auto pair : Detail::broadPhase(shapes, 0))
    if (!m_Impl->previous[pair.a].inverseMass && !m_Impl->previous[pair.b].inverseMass)
      if (auto hit = Detail::collideManifold(shapes[pair.a], shapes[pair.b], 0)) result.push_back(std::move(*hit));
  return result;
}
void System::reset() noexcept {
  m_Impl->cache.clear(); m_Impl->contacts.clear(); m_Impl->previous.clear();
  m_Impl->forces.clear(); m_Impl->fields.clear();
  m_Impl->previousDt = 0; m_Impl->stats = {};
}
void System::applyImpulse(Scene& scene, Entity entity, Vec3 p, Vec3 worldPoint) {
  finite(p); finite(worldPoint);
  if (!scene.owns(entity) || !entity.has<RigidBody>() || !entity.has<Collider>() || !entity.has<Transform>())
    throw std::invalid_argument("Impulse requires a live Transform/Collider/RigidBody entity.");
  if (entity.get<Collider>().empty())
    throw std::invalid_argument("Impulse requires a nonempty Collider.");
  auto b = body(scene, Detail::resolveCollider(entity, scene.worldMatrix(entity)));
  if (!b.inverseMass) return;
  const auto nextVelocity = (b.velocity + v(p) * b.inverseMass).vector();
  const auto nextAngular = (b.angular + b.inertia(collisionCross(v(worldPoint) - b.shape.center, v(p)))).vector();
  auto& rb = entity.get<RigidBody>(); rb.velocity = nextVelocity; rb.angularVelocity = nextAngular;
  for (auto& contact : m_Impl->cache) { contact.impulse = 0; contact.tangent = {}; }
}
void System::step(Scene& scene, float dt) {
  if (!std::isfinite(dt) || dt <= 0) throw std::invalid_argument("Physics step must be positive and finite.");
  const auto& config = m_Impl->config;
  auto environment = scene.physics();
  environment.validate(scene);
  const bool environmentChanged = environment.fields != m_Impl->fields ||
    environment.integration != m_Impl->integration;
  for (const auto entity : scene.query<RigidBody>()) {
    if (entity.has<Collider>() && entity.get<Collider>().empty()) continue;
    const auto& rb = entity.get<RigidBody>();
    validate(rb);
    if (rb.mass > 0 && (!entity.has<Transform>() || !entity.has<Collider>() || entity.get<Collider>().empty()))
      throw std::invalid_argument("Dynamic RigidBody requires Transform and Collider.");
  }
  auto proxies = Detail::captureColliders(scene);
  std::vector<Body> bodies;
  bodies.reserve(proxies.size());
  Statistics stats; stats.steps = m_Impl->stats.steps + 1;
  // Validate the complete input before integrating or mutating any component.
  for (const auto& shape : proxies) {
    auto b = body(scene, shape);
    const auto old = std::lower_bound(m_Impl->previous.begin(), m_Impl->previous.end(), shape.entity.id(),
      [](const Body& previous, auto id) { return previous.shape.entity.id() < id; });
    const bool found = old != m_Impl->previous.end() && old->shape.entity == shape.entity;
    b.edited = environmentChanged || !found || !same(b, *old);
    b.geometryEdited = !found || !sameGeometry(b, *old);
    if (b.inverseMass > 0) ++stats.dynamicBodies; else ++stats.staticBodies;
    bodies.push_back(b);
  }
  auto* jobs = m_Impl->jobs;
  auto fieldForces = sampleForces(environment, fieldBodies(bodies));
  std::vector<V> velocityBeforeContacts;
  std::vector<BodyForceSnapshot> forces;
  std::vector<unsigned char> dynamic(bodies.size());
  for (std::size_t i = 0; i < bodies.size(); ++i) dynamic[i] = bodies[i].inverseMass > 0;
  std::vector<Detail::ColliderPair> pairs;
  std::vector<std::vector<Constraint>> pairConstraints;
  std::vector<Constraint> constraints;
  std::vector<std::vector<std::size_t>> islands;
  std::vector<Manifold> contacts;
  std::vector<Cached> cache;
  std::map<std::pair<std::uint64_t, std::uint64_t>, std::span<const Cached>> oldPairs;
  for (std::size_t begin = 0; begin < m_Impl->cache.size();) {
    auto end = begin + 1;
    const auto& first = m_Impl->cache[begin];
    while (end < m_Impl->cache.size() && m_Impl->cache[end].a == first.a && m_Impl->cache[end].b == first.b) ++end;
    oldPairs.emplace(std::pair{first.a.id(), first.b.id()}, std::span<const Cached>{m_Impl->cache}.subspan(begin, end - begin));
    begin = end;
  }
  const auto integrate = [&](std::size_t index) {
    auto& b = bodies[index];
    Internal::integrate(b, fieldForces[index], dt, environment.integration);
  };
  const auto finishIntegration = [&] {
    if (environment.integration == Integration::VelocityVerlet) {
      const auto next = sampleForces(environment, fieldBodies(bodies));
      for (std::size_t i = 0; i < bodies.size(); ++i) if (bodies[i].inverseMass > 0) {
        bodies[i].velocity = bodies[i].velocity + next[i] * (bodies[i].inverseMass * dt * 0.5);
        fieldForces[i] = (fieldForces[i] + next[i]) * 0.5;
      }
    }
    velocityBeforeContacts.reserve(bodies.size());
    for (const auto& b : bodies) velocityBeforeContacts.push_back(b.velocity);
  };
  const auto bounds = [&](std::size_t index) {
    auto& b = bodies[index];
    if (b.inverseMass > 0) Detail::updateBounds(b.shape);
    proxies[index] = b.shape;
  };
  const auto broad = [&] {
    pairs = Detail::broadPhase(proxies, config.contactDistance, jobs, dynamic);
    stats.candidatePairs = pairs.size();
    pairConstraints.resize(pairs.size());
  };
  const auto narrow = [&](std::size_t index) {
    const auto i = pairs[index].a, j = pairs[index].b;
    const auto& a = bodies[i]; const auto& b = bodies[j];
    const auto found = oldPairs.find({a.shape.entity.id(), b.shape.entity.id()});
    const auto previousContacts = found == oldPairs.end() ? std::span<const Cached>{} : found->second;
    pairConstraints[index] = buildConstraints(a, b, i, j, previousContacts, config, dt, m_Impl->previousDt);
  };
  const auto prepare = [&] {
    for (auto& list : pairConstraints) constraints.insert(constraints.end(), list.begin(), list.end());
    islands = contactIslands(constraints, dynamic);
    stats.islands = islands.size();
  };
  const auto solve = [&](std::size_t index) { Internal::solveIsland(bodies, constraints, islands[index], config); };
  const auto finalize = [&] {
    // Prepare every output and cache allocation before committing Scene changes.
    cache.reserve(constraints.size());
    forces.reserve(bodies.size());
    for (std::size_t i = 0; i < bodies.size(); ++i) if (bodies[i].inverseMass > 0) {
      const auto& b = bodies[i];
      const auto contact = (b.velocity - velocityBeforeContacts[i]) * (static_cast<double>(b.properties.mass) / dt);
      forces.push_back({b.shape.entity, b.shape.center.vector(), fieldForces[i].vector(),
        contact.vector(), (fieldForces[i] + contact).vector(), true});
    }
    for (auto& b : bodies) if (b.inverseMass > 0) {
      b.pose.position = b.shape.center.vector();
      b.properties.velocity = b.velocity.vector(); b.properties.angularVelocity = b.angular.vector();
      Detail::updateBounds(b.shape);
    }
    for (const auto& c : constraints) {
      const auto& a = bodies[c.a]; const auto& b = bodies[c.b];
      if (!validAnchor(a, b, c.localA, c.localB, c.normal, config.contactDistance)) continue;
      const auto pa = a.shape.center + a.offset(c.localA), pb = b.shape.center + b.offset(c.localB);
      const auto friction = c.tangent1 * c.tangentImpulse1 + c.tangent2 * c.tangentImpulse2;
      ContactPoint p{pa.vector(), pb.vector(), V{collisionDot(pb - pa, c.normal), 0, 0}.vector().x,
        V{c.normalImpulse, 0, 0}.vector().x, friction.vector()};
      if (contacts.empty() || contacts.back().a != a.shape.entity || contacts.back().b != b.shape.entity)
        contacts.push_back({a.shape.entity, b.shape.entity, c.normal.vector(), {}});
      contacts.back().points.push_back(p);
      cache.push_back({a.shape.entity, b.shape.entity, c.normal, c.localA, c.localB, friction, c.normalImpulse});
      ++stats.contacts;
      stats.maximumPenetration = std::max(stats.maximumPenetration, -p.separation);
    }
  };
  if (jobs) {
    TaskGraph graph;
    const JobOptions physics{JobCategory::Physics};
    const auto integration = graph.addParallelFor(bodies.size(), integrate, physics);
    const auto fieldUpdate = graph.add(finishIntegration, physics);
    const auto update = graph.addParallelFor(bodies.size(), bounds, physics);
    const auto candidates = graph.add(broad, physics);
    const auto collision = graph.addParallelFor([&] { return pairs.size(); }, narrow, physics);
    const auto preparation = graph.add(prepare, physics);
    const auto solving = graph.addParallelFor([&] { return islands.size(); }, solve, {JobCategory::Physics, {}, 1});
    const auto output = graph.add(finalize, physics);
    graph.depends(fieldUpdate, integration); graph.depends(update, fieldUpdate); graph.depends(candidates, update);
    graph.depends(collision, candidates); graph.depends(preparation, collision);
    graph.depends(solving, preparation); graph.depends(output, solving);
    jobs->wait(jobs->submit(graph), WaitMode::Help);
  } else {
    for (std::size_t i = 0; i < bodies.size(); ++i) integrate(i);
    finishIntegration();
    for (std::size_t i = 0; i < bodies.size(); ++i) bounds(i);
    broad();
    for (std::size_t i = 0; i < pairs.size(); ++i) narrow(i);
    prepare();
    for (std::size_t i = 0; i < islands.size(); ++i) solve(i);
    finalize();
  }
  for (const auto& b : bodies) if (b.inverseMass > 0) {
    b.shape.entity.get<Transform>() = b.pose;
    b.shape.entity.get<RigidBody>() = b.properties;
  }
  m_Impl->cache = std::move(cache); m_Impl->previous = std::move(bodies);
  m_Impl->contacts = std::move(contacts); m_Impl->stats = stats; m_Impl->previousDt = dt;
  m_Impl->forces = std::move(forces); m_Impl->fields = std::move(environment.fields);
  m_Impl->integration = environment.integration;
}
} // namespace Jade::Physics
