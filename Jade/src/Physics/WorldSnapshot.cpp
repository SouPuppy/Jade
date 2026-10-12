#include "../internal/Physics/WorldImpl.hpp"

namespace Jade::Physics {
struct WorldSnapshot::Impl {
  std::unique_ptr<World> queryWorld;
  std::uint64_t identity = 0;
  std::vector<BodyID> local,original;
  std::vector<BodyState> states;
  std::vector<ForceState> forces;
  std::vector<WorldContact> contacts;
  BodyID require(BodyID id) const {
    if (id.world != identity || id.slot >= local.size() || !local[id.slot] || original[local[id.slot].slot] != id)
      throw std::invalid_argument("BodyID does not exist in this snapshot.");
    return local[id.slot];
  }
  QueryFilter filter(QueryFilter input) const {
    if (input.ignored) {
      if (input.ignored.world == identity && input.ignored.slot < local.size() && local[input.ignored.slot] && original[local[input.ignored.slot].slot] == input.ignored)
        input.ignored = local[input.ignored.slot];
      else input.ignored = {};
    }
    if (input.acceptHit) {
      const auto accept = std::move(input.acceptHit);
      input.acceptHit = [this,accept](QueryHit hit) { hit.body = original[hit.body.slot]; return accept(hit); };
    }
    return input;
  }
  std::optional<QueryHit> translate(std::optional<QueryHit> hit) const { if (hit) hit->body = original[hit->body.slot]; return hit; }
};
WorldSnapshot World::snapshot() const {
  auto& p = *m_Impl; p.writable();
  auto copy = std::make_shared<WorldSnapshot::Impl>(); copy->identity = p.identity;
  copy->queryWorld = std::make_unique<World>(p.config); copy->queryWorld->setFields({}); copy->queryWorld->reserve(p.slots.size());
  copy->local.resize(p.slots.size()); copy->states.resize(p.slots.size()); copy->forces.resize(p.slots.size());
  for (std::size_t i = 0; i < p.slots.size(); ++i) if (p.slots[i].alive) {
    const auto id = p.id(i); const auto local = copy->queryWorld->create(description(id));
    copy->local[i] = local; copy->original.push_back(id); copy->states[i] = state(id); copy->forces[i] = forces(id);
  }
  copy->contacts = p.contacts;
  // Finish lazy index construction before publishing to concurrent readers.
  constexpr double limit = std::numeric_limits<float>::max();
  (void)copy->queryWorld->query({{-limit,-limit,-limit},{limit,limit,limit}});
  WorldSnapshot result; result.m_Impl = std::move(copy); return result;
}
BodyState WorldSnapshot::state(BodyID id) const {
  if (!m_Impl) throw std::logic_error("Empty WorldSnapshot."); m_Impl->require(id); return m_Impl->states[id.slot];
}
ForceState WorldSnapshot::forces(BodyID id) const {
  if (!m_Impl) throw std::logic_error("Empty WorldSnapshot."); m_Impl->require(id); return m_Impl->forces[id.slot];
}
std::span<const WorldContact> WorldSnapshot::contacts() const { return m_Impl ? std::span<const WorldContact>{m_Impl->contacts} : std::span<const WorldContact>{}; }
std::vector<BodyID> WorldSnapshot::query(const Aabb& b,QueryFilter filter) const {
  if (!m_Impl) return {}; auto result = m_Impl->queryWorld->query(b,m_Impl->filter(std::move(filter)));
  for (auto& id : result) id = m_Impl->original[id.slot]; return result;
}
std::vector<BodyID> WorldSnapshot::overlap(const BodyDescription& b,QueryFilter filter) const {
  if (!m_Impl) return {}; auto result = m_Impl->queryWorld->overlap(b,m_Impl->filter(std::move(filter)));
  for (auto& id : result) id = m_Impl->original[id.slot]; return result;
}
std::optional<QueryHit> WorldSnapshot::rayCast(Vector o,Vector d,QueryFilter filter) const {
  if (!m_Impl) return {}; return m_Impl->translate(m_Impl->queryWorld->rayCast(o,d,m_Impl->filter(std::move(filter))));
}
std::optional<QueryHit> WorldSnapshot::shapeCast(const BodyDescription& b,Vector d,QueryFilter filter) const {
  if (!m_Impl) return {}; return m_Impl->translate(m_Impl->queryWorld->shapeCast(b,d,m_Impl->filter(std::move(filter))));
}
} // namespace Jade::Physics
