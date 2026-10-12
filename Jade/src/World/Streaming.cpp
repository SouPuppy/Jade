#include <Jade/World/Streaming.hpp>
#include <Jade/World/World.hpp>
#include <Jade/Core/JobSystem.hpp>
#include <Jade/Core/Transform.hpp>
#include <json.hpp>
#include <atomic>
#include <climits>
#include <cmath>
#include <set>

namespace Jade {
ChunkCoord chunkAt(Vec3 p, float size) {
  if (!std::isfinite(size) || size <= 0 || !std::isfinite(p.x) || !std::isfinite(p.z)) throw std::invalid_argument("Invalid chunk position.");
  const auto x = std::floor(static_cast<double>(p.x) / size), z = std::floor(static_cast<double>(p.z) / size);
  if (x < INT_MIN || x > INT_MAX || z < INT_MIN || z > INT_MAX) throw std::out_of_range("Chunk coordinate overflow.");
  return {static_cast<int>(x), static_cast<int>(z)};
}
namespace {
void validateSource(const StreamingSource& s) {
  (void)chunkAt(s.position);
  if (s.dataRadius < 0 || s.dataRadius > 128 || s.renderRadius < -1 || s.collisionRadius < -1 || s.simulationRadius < -1 ||
      s.renderRadius > s.dataRadius || s.collisionRadius > s.dataRadius || s.simulationRadius > s.dataRadius)
    throw std::invalid_argument("Streaming radii must fit within data (0..128); -1 disables a runtime dimension.");
}
struct Demand { bool render = false, collision = false, simulation = false; };
bool runtimeRequired(Demand d) { return d.render || d.collision || d.simulation; }
constexpr auto capacityError = "Streaming demand exceeds the resident chunk budget.";
}
struct StreamingManager::Impl {
  World& world;
  StreamingSettings settings;
  std::shared_ptr<const ChunkProvider> provider;
  std::shared_ptr<WorldStore> store;
  std::map<std::string, ObjectCodec> codecs;
  JobSystem* jobs = nullptr;
  WorldState state, durable;
  std::map<std::uint64_t, StreamingSource> sources;
  std::uint64_t nextSource = 1, session = 0;
  struct Chunk {
    ChunkData data;
    Demand demand;
    float absent = 0, idle = 0;
    bool materialized = false;
  };
  struct Live { ChunkCoord coord; ObjectData data; Entity entity; bool pendingMigration = false; };
  struct Work {
    ChunkCoord coord;
    std::uint64_t session;
    JobHandle job;
    ChunkData data;
    std::string error;
    std::atomic<bool> done = false;
  };
  struct SaveWork {
    JobHandle job; std::string error; std::atomic<bool> done = false;
    std::set<ChunkCoord> releases;
    WorldState snapshot;
  };
  std::shared_ptr<SaveWork> saveWork;
  std::vector<std::shared_ptr<Work>> work;
  std::map<ChunkCoord, Chunk> chunks;
  std::map<ObjectId, Live> live;
  std::set<ChunkCoord> failed, pins;
  std::set<ChunkCoord> releaseTickets;
  bool pendingSave = false;
  StreamingStatistics stats;
  float checkpoint = 0;
  bool unloading = false;
  explicit Impl(World& w) : world(w) {}
  void membership(Entity root, Residency value) {
    std::vector<Entity> entities{root};
    for (std::size_t i = 0; i < entities.size(); ++i) {
      auto children = world.children(entities[i]); entities.insert(entities.end(), children.begin(), children.end());
      auto e = entities[i];
      if (!e.has<Residency>()) e.add<Residency>(value);
      else if (e.get<Residency>() != value) { e.get<Residency>() = value; world.registry().markChanged(e); }
    }
  }
  void apply() {
    for (auto& [id, object] : live) {
      if (!object.entity) continue;
      const auto it = chunks.find(object.coord);
      const auto d = it == chunks.end() ? Demand{} : it->second.demand;
      const bool simulation = d.simulation && !object.pendingMigration;
      membership(object.entity, {d.render, d.collision && (!object.data.dynamic || simulation), simulation});
    }
  }
  WorldState capture() {
    auto snapshot = state;
    for (auto& [id, object] : live) if (object.data.persistent && object.entity) {
      auto data = object.data;
      data.state = codecs.at(data.type).capture(world, object.entity);
      (void)nlohmann::json::parse(data.state);
      auto coord = object.coord;
      if (object.data.dynamic && object.entity.has<Transform>()) {
        const auto destination = chunkAt(object.entity.get<Transform>().position,settings.chunkSize);
        if (provider->contains(destination)) coord = destination;
      }
      snapshot.objects[id] = {coord, std::move(data), false};
    }
    return snapshot;
  }
  bool safeRelease(ChunkCoord coord) const {
    // Compare the actual codec state, including direct component edits, with
    // the acknowledged snapshot. An older save cannot authorize newer data.
    for (const auto& [id, saved] : state.objects) if (saved.coord == coord) {
      auto found = durable.objects.find(id);
      if (found == durable.objects.end() || found->second != saved) return false;
    }
    for (const auto& [id, object] : live) if (object.coord == coord && object.data.persistent && object.entity) {
      auto found = durable.objects.find(id);
      if (found == durable.objects.end() || found->second.deleted || found->second.coord != coord ||
          found->second.data.state != codecs.at(object.data.type).capture(world, object.entity)) return false;
    }
    return true;
  }
  void dematerialize(ChunkCoord coord) {
    unloading = true;
    for (auto it = live.begin(); it != live.end();) {
      if (it->second.coord != coord) { ++it; continue; }
      auto entity = it->second.entity; it = live.erase(it);
      if (entity) world.destroy(entity);
    }
    unloading = false;
    chunks.at(coord).materialized = false;
  }
  void instantiate(const ChunkData& data, Demand demand) {
    std::map<ObjectId, ObjectData> desired;
    for (auto& object : data.objects) {
      if (!object.id || !desired.emplace(object.id, object).second) throw std::invalid_argument("Duplicate chunk object ID.");
      const auto saved = state.objects.find(object.id);
      if (saved != state.objects.end()) {
        if (saved->second.deleted || saved->second.coord != data.coord) desired.erase(object.id);
        else desired[object.id] = saved->second.data;
      }
    }
    for (const auto& [id, saved] : state.objects)
      if (!saved.deleted && saved.coord == data.coord) desired[id] = saved.data;
    for (const auto& [id, object] : desired) {
      if (!codecs.contains(object.type)) throw std::invalid_argument("Unknown world object codec: " + object.type);
      (void)nlohmann::json::parse(object.state);
      if (const auto existing = live.find(id); existing != live.end() && existing->second.coord != data.coord) {
        const auto& incoming = existing->second;
        if (!incoming.data.dynamic || !incoming.entity.has<Transform>() ||
            chunkAt(incoming.entity.get<Transform>().position,settings.chunkSize) != data.coord)
          throw std::invalid_argument("Object identity occurs in multiple chunks.");
      }
    }
    std::vector<std::pair<ObjectData, Entity>> created;
    try {
      for (const auto& [id, object] : desired) {
        if (live.contains(id)) continue;
        auto root = world.create(); created.emplace_back(object, root);
        codecs.at(object.type).spawn(world, root, object);
        membership(root, {demand.render, demand.collision && (!object.dynamic || demand.simulation), demand.simulation});
        if (object.persistent) root.add<PersistentObject>(ObjectRef{settings.world, id});
      }
      for (const auto& [object, root] : created) live.emplace(object.id, Live{data.coord, object, root});
      chunks.at(data.coord).materialized = true;
    } catch (...) {
      unloading = true;
      for (const auto& [object, root] : created) {
        live.erase(object.id); if (root) world.destroy(root);
      }
      unloading = false; throw;
    }
  }
};
StreamingManager::StreamingManager(World& world) : m_Impl(std::make_unique<Impl>(world)) {}
StreamingManager::~StreamingManager() {
  // Accepted jobs retain only immutable provider/store data, never the World.
  if (m_Impl->jobs) {
    for (auto& work : m_Impl->work) if (work->job.valid()) m_Impl->jobs->wait(work->job);
    if (m_Impl->saveWork && m_Impl->saveWork->job.valid()) m_Impl->jobs->wait(m_Impl->saveWork->job);
  }
}
void StreamingManager::configure(StreamingSettings settings, std::shared_ptr<const ChunkProvider> provider,
  std::map<std::string, ObjectCodec> codecs, std::shared_ptr<WorldStore> store, JobSystem* jobs) {
  if (!provider || !std::isfinite(settings.chunkSize) || settings.chunkSize <= 0 || settings.world.empty() ||
      !std::isfinite(settings.releaseDelay) || settings.releaseDelay < 0 || !std::isfinite(settings.checkpointInterval) || settings.checkpointInterval <= 0 ||
      !settings.maxLoads || !settings.maxCommits || !settings.maxUnloads || !settings.maxResidentChunks)
    throw std::invalid_argument("Invalid streaming configuration.");
  if (m_Impl->provider) throw std::logic_error("Streaming is already configured; create a new World.");
  for (const auto& [name, codec] : codecs) if (!codec.spawn || !codec.capture) throw std::invalid_argument("Incomplete object codec.");
  auto state = store ? store->load(settings.world) : WorldState{settings.world, {}};
  if (state.world != settings.world) throw std::invalid_argument("Store returned a different World ID.");
  m_Impl->settings = std::move(settings); m_Impl->provider = std::move(provider); m_Impl->codecs = std::move(codecs);
  m_Impl->store = std::move(store); m_Impl->jobs = jobs; m_Impl->state = state;
  m_Impl->durable = std::move(state); ++m_Impl->session;
}
std::uint64_t StreamingManager::addSource(StreamingSource source) {
  validateSource(source); const auto id = m_Impl->nextSource++; m_Impl->sources.emplace(id, source); return id;
}
void StreamingManager::setSource(std::uint64_t id, StreamingSource source) { validateSource(source); m_Impl->sources.at(id) = source; }
void StreamingManager::removeSource(std::uint64_t id) { m_Impl->sources.erase(id); }
void StreamingManager::update(float dt) {
  if (!std::isfinite(dt) || dt < 0) throw std::invalid_argument("Invalid streaming delta.");
  auto& s = *m_Impl;
  if (!s.provider) return;
  std::map<ChunkCoord, Demand> needed;
  for (const auto& [id, source] : s.sources) {
    auto center = chunkAt(source.position, s.settings.chunkSize);
    for (int z = -source.dataRadius; z <= source.dataRadius; ++z)
      for (int x = -source.dataRadius; x <= source.dataRadius; ++x) {
        const auto cx = static_cast<std::int64_t>(center.x) + x, cz = static_cast<std::int64_t>(center.z) + z;
        if (cx < INT_MIN || cx > INT_MAX || cz < INT_MIN || cz > INT_MAX) continue;
        ChunkCoord coord{static_cast<int>(cx), static_cast<int>(cz)};
        if (!s.provider->contains(coord)) continue;
        auto& d = needed[coord]; const auto distance = std::max(std::abs(x), std::abs(z));
        d.render |= distance <= source.renderRadius; d.collision |= distance <= source.collisionRadius; d.simulation |= distance <= source.simulationRadius;
      }
  }
  migrate();
  for (auto coord : s.pins) needed[coord] = {true, true, true};
  if (needed.size() > s.settings.maxResidentChunks) { s.stats.error = capacityError; return; }
  if (s.stats.error == capacityError) s.stats.error.clear();
  if (s.saveWork && s.saveWork->done.load(std::memory_order_acquire)) {
    if (!s.saveWork->error.empty()) s.stats.error = s.saveWork->error;
    else {
      ++s.stats.commits; s.durable = std::move(s.saveWork->snapshot);
      s.releaseTickets.insert(s.saveWork->releases.begin(), s.saveWork->releases.end());
    }
    s.saveWork.reset();
    if (s.pendingSave) { s.pendingSave = false; save(); }
  }
  unsigned commits = 0;
  for (auto it = s.work.begin(); it != s.work.end();) {
    auto work = *it;
    if (!work->done.load(std::memory_order_acquire)) { ++it; continue; }
    if (needed.contains(work->coord) && commits >= s.settings.maxCommits) { ++it; continue; }
    if (work->session == s.session && needed.contains(work->coord)) {
      try {
        if (!work->error.empty()) throw std::runtime_error(work->error);
        if (work->data.coord != work->coord) throw std::invalid_argument("Provider returned a different chunk.");
        std::set<ObjectId> identities;
        for (const auto& object : work->data.objects) {
          if (!object.id || !identities.insert(object.id).second || !s.codecs.contains(object.type))
            throw std::invalid_argument("Invalid object identity or codec in Chunk data.");
          (void)nlohmann::json::parse(object.state);
        }
        const auto coord = work->coord;
        auto [chunk, inserted] = s.chunks.emplace(coord, Impl::Chunk{std::move(work->data), needed.at(coord)});
        if (runtimeRequired(needed.at(coord))) s.instantiate(chunk->second.data, needed.at(coord));
        ++commits;
      } catch (const std::exception& e) { s.failed.insert(work->coord); s.stats.error = e.what(); }
    }
    it = s.work.erase(it);
  }
  for (auto& [coord, chunk] : s.chunks) {
    auto it = needed.find(coord);
    chunk.demand = it == needed.end() ? Demand{} : it->second;
    chunk.absent = it == needed.end() ? chunk.absent + dt : 0;
    chunk.idle = runtimeRequired(chunk.demand) ? 0 : chunk.idle + dt;
    if (runtimeRequired(chunk.demand)) s.releaseTickets.erase(coord);
    if (runtimeRequired(chunk.demand) && !chunk.materialized && !s.failed.contains(coord) && commits < s.settings.maxCommits) {
      try { s.instantiate(chunk.data, chunk.demand); ++commits; }
      catch (const std::exception& e) { s.failed.insert(coord); s.stats.error = e.what(); }
    }
  }
  s.apply();
  std::vector<ChunkCoord> releases;
  for (const auto& [coord, chunk] : s.chunks)
    if ((!needed.contains(coord) && (chunk.absent >= s.settings.releaseDelay || s.chunks.size() >= s.settings.maxResidentChunks)) ||
        (needed.contains(coord) && !runtimeRequired(chunk.demand) && chunk.materialized && chunk.idle >= s.settings.releaseDelay))
      releases.push_back(coord);
  if (!releases.empty()) {
    try {
      // Freeze and capture before release, then wait across frames for the
      // durable acknowledgement. Ordinary updates never block on file I/O.
      for (auto coord : releases) if (s.releaseTickets.contains(coord) && !s.safeRelease(coord)) s.releaseTickets.erase(coord);
      if (!s.saveWork && std::any_of(releases.begin(), releases.end(), [&](auto c) { return !s.releaseTickets.contains(c); })) {
        save();
        if (s.saveWork) s.saveWork->releases.insert(releases.begin(), releases.end());
        else s.releaseTickets.insert(releases.begin(), releases.end());
      }
      unsigned released = 0;
      for (auto coord : releases) {
        if (!s.releaseTickets.contains(coord)) continue;
        if (released++ >= s.settings.maxUnloads) break;
        s.dematerialize(coord);
        if (!needed.contains(coord)) s.chunks.erase(coord);
        s.releaseTickets.erase(coord);
      }
    } catch (const std::exception& e) { s.unloading = false; s.stats.error = e.what(); }
  }
  std::vector<ChunkCoord> candidates;
  for (const auto& [coord, demand] : needed) {
    if (s.chunks.contains(coord) || s.failed.contains(coord)) continue;
    if (std::any_of(s.work.begin(), s.work.end(), [&](const auto& w) { return w->coord == coord; })) continue;
    candidates.push_back(coord);
  }
  auto distance = [&](ChunkCoord coord) {
    std::int64_t nearest = INT64_MAX;
    for (const auto& [id, source] : s.sources) {
      auto c = chunkAt(source.position, s.settings.chunkSize);
      nearest = std::min(nearest, std::max(std::abs(static_cast<std::int64_t>(c.x) - coord.x), std::abs(static_cast<std::int64_t>(c.z) - coord.z)));
    }
    return nearest;
  };
  std::stable_sort(candidates.begin(), candidates.end(), [&](auto a, auto b) { return distance(a) < distance(b); });
  for (auto coord : candidates) {
    if (s.work.size() >= s.settings.maxLoads || s.chunks.size() + s.work.size() >= s.settings.maxResidentChunks) break;
    auto work = std::make_shared<Impl::Work>(); work->coord = coord; work->session = s.session;
    auto load = [work, provider = s.provider] {
      try { work->data = provider->load(work->coord); } catch (const std::exception& e) { work->error = e.what(); }
      catch (...) { work->error = "Unknown chunk provider failure."; }
      work->done.store(true, std::memory_order_release);
    };
    if (s.jobs) work->job = s.jobs->submit(load, JobCategory::Asset); else load();
    s.work.push_back(std::move(work));
  }
  s.checkpoint += dt;
  if (s.checkpoint >= s.settings.checkpointInterval) {
    s.checkpoint = 0;
    try { save(); } catch (const std::exception& e) { s.stats.error = e.what(); }
  }
  s.stats.data = static_cast<unsigned>(s.chunks.size()); s.stats.loading = static_cast<unsigned>(s.work.size());
  s.stats.active = s.stats.rendered = s.stats.collision = s.stats.frozen = 0;
  for (const auto& [coord, chunk] : s.chunks) {
    s.stats.active += chunk.materialized && chunk.demand.simulation;
    s.stats.rendered += chunk.materialized && chunk.demand.render;
    s.stats.collision += chunk.materialized && chunk.demand.collision;
  }
  for (const auto& [id, object] : s.live) if (object.data.dynamic && object.entity && !object.entity.get<Residency>().simulation) ++s.stats.frozen;
  s.stats.saving = static_cast<bool>(s.saveWork);
}
bool StreamingManager::ready(ChunkCoord coord, bool collision) const {
  const auto it = m_Impl->chunks.find(coord);
  return it != m_Impl->chunks.end() && (!collision || (it->second.materialized && it->second.demand.collision));
}
void StreamingManager::barrier() {
  for (;;) {
    update(0);
    if (!m_Impl->stats.error.empty()) throw std::runtime_error(m_Impl->stats.error);
    const auto& s = *m_Impl;
    const bool promotions = std::any_of(s.chunks.begin(), s.chunks.end(), [](const auto& entry) {
      return runtimeRequired(entry.second.demand) && !entry.second.materialized;
    });
    if (s.work.empty() && !promotions && !s.saveWork) break;
    if (m_Impl->jobs) for (const auto& work : m_Impl->work) if (work->job.valid()) m_Impl->jobs->wait(work->job);
    if (m_Impl->jobs && m_Impl->saveWork && m_Impl->saveWork->job.valid()) m_Impl->jobs->wait(m_Impl->saveWork->job);
  }
}
Entity StreamingManager::resolve(const ObjectRef& ref) const {
  if (ref.world != m_Impl->settings.world) return {};
  const auto it = m_Impl->live.find(ref.object); return it == m_Impl->live.end() ? Entity{} : it->second.entity;
}
void StreamingManager::track(Entity e, ChunkCoord coord, ObjectData data) {
  auto& s = *m_Impl;
  if (!s.world.owns(e) || s.world.parent(e) || !data.id || !s.codecs.contains(data.type) || s.live.contains(data.id) ||
      s.state.objects.contains(data.id) ||
      !s.chunks.contains(coord) || !s.chunks.at(coord).materialized) throw std::invalid_argument("Invalid tracked object.");
  (void)nlohmann::json::parse(data.state);
  if (data.persistent) e.add<PersistentObject>(ObjectRef{s.settings.world, data.id});
  const auto demand = s.chunks.at(coord).demand;
  s.membership(e,{demand.render,demand.collision && (!data.dynamic || demand.simulation),demand.simulation});
  s.live.emplace(data.id, Impl::Live{coord, std::move(data), e});
}
void StreamingManager::destroyed(Entity entity) {
  auto& s = *m_Impl;
  if (s.unloading) return;
  for (auto it = s.live.begin(); it != s.live.end();) {
    bool doomed = false;
    for (auto e = it->second.entity; e; e = s.world.parent(e)) if (e == entity) { doomed = true; break; }
    if (!doomed) { ++it; continue; }
    const auto& object = it->second;
    if (object.data.persistent) s.state.objects[it->first] = {object.coord, object.data, true};
    it = s.live.erase(it);
  }
}
void StreamingManager::validateParent(Entity child, Entity parent) const {
  auto owner = [&](Entity e) -> const Impl::Live* {
    while (e) {
      for (const auto& [id, object] : m_Impl->live) if (object.entity == e) return &object;
      e = m_Impl->world.parent(e);
    }
    return nullptr;
  };
  const auto a = owner(child), b = owner(parent);
  if (a && a->entity == child && parent) throw std::invalid_argument("Streamed object roots cannot be parented.");
  if (a && b && a->coord != b->coord) throw std::invalid_argument("Cross-chunk parenting is forbidden.");
  if (a && !b && parent) throw std::invalid_argument("Cross-domain parenting is forbidden.");
  if (a && !parent && child != a->entity) throw std::invalid_argument("A streamed child cannot escape its ownership tree.");
}
void StreamingManager::migrate() {
  auto& s = *m_Impl; s.pins.clear();
  if (!s.provider) return;
  for (auto& [id, object] : s.live) {
    object.pendingMigration = false;
    if (!object.data.dynamic || !object.entity || !object.entity.has<Transform>()) continue;
    const auto coord = chunkAt(object.entity.get<Transform>().position, s.settings.chunkSize);
    if (coord == object.coord || !s.provider->contains(coord)) continue;
    if (!s.chunks.contains(coord) || !s.chunks.at(coord).materialized) {
      s.pins.insert(coord); s.pins.insert(object.coord); object.pendingMigration = true;
      const bool render = s.chunks.contains(object.coord) && s.chunks.at(object.coord).demand.render;
      s.membership(object.entity,{render,false,false});
      continue;
    }
    object.coord = coord;
    if (object.data.persistent) {
      auto data = object.data; data.state = s.codecs.at(data.type).capture(s.world, object.entity);
      s.state.objects[id] = {coord, std::move(data), false};
    }
  }
}
void StreamingManager::save() {
  auto& s = *m_Impl;
  if (s.saveWork) { s.pendingSave = true; return; }
  auto snapshot = s.capture(); s.state = snapshot;
  if (!s.store) { s.durable = std::move(snapshot); return; }
  auto work = std::make_shared<Impl::SaveWork>();
  work->snapshot = std::move(snapshot);
  auto save = [work, store = s.store] {
    try { store->save(work->snapshot); } catch (const std::exception& e) { work->error = e.what(); }
    catch (...) { work->error = "Unknown world save failure."; }
    work->done.store(true, std::memory_order_release);
  };
  if (s.jobs) work->job = s.jobs->submit(save, JobCategory::Asset); else save();
  s.saveWork = std::move(work);
}
void StreamingManager::flush() {
  auto& s = *m_Impl; if (!s.saveWork) return;
  if (s.jobs && s.saveWork->job.valid()) s.jobs->wait(s.saveWork->job);
  const auto error = s.saveWork->error;
  if (error.empty()) {
    s.durable = std::move(s.saveWork->snapshot);
    s.releaseTickets.insert(s.saveWork->releases.begin(), s.saveWork->releases.end());
  }
  s.saveWork.reset(); s.stats.saving = false;
  if (!error.empty()) { s.stats.error = error; throw std::runtime_error(error); }
  ++s.stats.commits;
  if (s.pendingSave) { s.pendingSave = false; save(); flush(); }
}
void StreamingManager::retry() { m_Impl->failed.clear(); m_Impl->stats.error.clear(); }
void StreamingManager::detachJobs() noexcept { m_Impl->jobs = nullptr; }
const StreamingStatistics& StreamingManager::statistics() const noexcept { return m_Impl->stats; }
const StreamingSettings& StreamingManager::settings() const noexcept { return m_Impl->settings; }
}
