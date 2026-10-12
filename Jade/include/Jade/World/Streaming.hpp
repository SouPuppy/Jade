#pragma once
#include <Jade/Core/Entity.hpp>
#include <Jade/Math/Vector.hpp>
#include <filesystem>
#include <compare>
#include <functional>
#include <map>
#include <string>

namespace Jade {
class World;
class JobSystem;
struct ChunkCoord {
  int x = 0, z = 0;
  auto operator<=>(const ChunkCoord&) const = default;
};
ChunkCoord chunkAt(Vec3 position, float chunkSize = 16);
using ObjectId = std::uint64_t;
struct ObjectRef { std::string world; ObjectId object = 0; };
// A codec owns the versioned JSON state; runtime IDs and pointers never cross I/O.
struct ObjectData {
  ObjectId id = 0;
  std::string type, state = "{}";
  bool persistent = true, dynamic = false;
  bool operator==(const ObjectData&) const = default;
};
struct ChunkData { ChunkCoord coord; std::vector<ObjectData> objects; };
struct ObjectState {
  ChunkCoord coord; ObjectData data; bool deleted = false;
  bool operator==(const ObjectState&) const = default;
};
struct WorldState { std::string world; std::map<ObjectId, ObjectState> objects; };
struct Residency {
  bool render = true, collision = true, simulation = true;
  bool operator==(const Residency&) const = default;
};
struct PersistentObject { ObjectRef reference; };
class ChunkProvider {
public:
  virtual ~ChunkProvider() = default;
  virtual bool contains(ChunkCoord) const noexcept = 0;
  // Concurrent, pure CPU work. No Registry, Assets or graphics access.
  virtual ChunkData load(ChunkCoord) const = 0;
};
class WorldStore {
public:
  virtual ~WorldStore() = default;
  virtual WorldState load(const std::string& world) const = 0;
  virtual void save(const WorldState&) = 0;
};
class FileWorldStore final : public WorldStore {
public:
  explicit FileWorldStore(std::filesystem::path directory);
  WorldState load(const std::string&) const override;
  void save(const WorldState&) override;
private:
  std::filesystem::path m_Directory;
};
struct ObjectCodec {
  // Spawn must use the supplied root. Children are rolled back with the root.
  std::function<void(World&, Entity root, const ObjectData&)> spawn;
  std::function<std::string(const World&, Entity)> capture;
};
struct StreamingSettings {
  std::string world = "world";
  float chunkSize = 16, releaseDelay = 1, checkpointInterval = 5;
  unsigned maxLoads = 4, maxCommits = 2, maxUnloads = 2;
  std::size_t maxResidentChunks = 256;
};
struct StreamingSource {
  Vec3 position;
  // -1 disables a runtime dimension; dataRadius must be nonnegative.
  int dataRadius = 2, renderRadius = 2, collisionRadius = 2, simulationRadius = 1;
};
struct StreamingStatistics {
  unsigned data = 0, rendered = 0, collision = 0, active = 0, loading = 0, frozen = 0;
  bool saving = false;
  std::string error;
  std::uint64_t commits = 0;
};
class StreamingManager final {
public:
  explicit StreamingManager(World&);
  ~StreamingManager();
  StreamingManager(const StreamingManager&) = delete;
  StreamingManager& operator=(const StreamingManager&) = delete;
  void configure(StreamingSettings, std::shared_ptr<const ChunkProvider>,
                 std::map<std::string, ObjectCodec>, std::shared_ptr<WorldStore> = {}, JobSystem* = nullptr);
  std::uint64_t addSource(StreamingSource);
  void setSource(std::uint64_t, StreamingSource);
  void removeSource(std::uint64_t);
  void update(float dt);
  // Explicit startup/teleport barrier, outside the ordinary frame loop.
  void barrier();
  bool ready(ChunkCoord, bool collision = true) const;
  Entity resolve(const ObjectRef&) const;
  void track(Entity, ChunkCoord, ObjectData);
  void destroyed(Entity);
  void validateParent(Entity, Entity) const;
  void migrate();
  void save();
  void flush();
  void retry();
  void detachJobs() noexcept;
  const StreamingStatistics& statistics() const noexcept;
  const StreamingSettings& settings() const noexcept;
private:
  struct Impl;
  std::unique_ptr<Impl> m_Impl;
};
}
