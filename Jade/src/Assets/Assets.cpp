#include "Assets.hpp"

#include "../internal/Mesh/MeshValidation.hpp"
#include "../internal/Mesh/ObjLoader.hpp"

#include <Jade/Log.hpp>

#include <chrono>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace Jade {
namespace {

const Log::Logger logger{"Assets"};

} // namespace
namespace Detail {

class AssetStore final {
public:
  struct Entry {
    std::type_index type;
    std::shared_ptr<const void> resource;
  };
  std::vector<Entry> entries;
  std::unordered_map<std::filesystem::path, MeshHandle> meshes;
};

} // namespace Detail

Assets::Assets() : m_Store(std::make_shared<Detail::AssetStore>()) {}
Assets::~Assets() = default;

void Assets::ValidateMesh(const Mesh& mesh)
{
  Detail::ValidateMesh(mesh);
}

std::uint64_t Assets::Add(std::type_index type, std::shared_ptr<const void> resource)
{
  if (m_Store->entries.size() == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("Assets has exhausted its resource identifiers.");
  }
  m_Store->entries.push_back({type, std::move(resource)});
  return static_cast<std::uint64_t>(m_Store->entries.size());
}

const void* Assets::Get(const std::weak_ptr<Detail::AssetStore>& owner, std::uint64_t id,
  std::type_index type) const
{
  if (owner.lock() != m_Store || id == 0 || id > m_Store->entries.size()) {
    throw std::invalid_argument("Asset handle is empty, expired or belongs to another Assets owner.");
  }
  const auto& entry = m_Store->entries[static_cast<std::size_t>(id - 1)];
  if (entry.type != type) {
    throw std::invalid_argument("Asset handle has the wrong resource type.");
  }
  return entry.resource.get();
}

std::shared_ptr<const void> Assets::Share(const std::weak_ptr<Detail::AssetStore>& owner,
  std::uint64_t id, std::type_index type) const
{
  (void)Get(owner, id, type);
  return m_Store->entries[static_cast<std::size_t>(id - 1)].resource;
}

MeshHandle Assets::LoadMesh(const std::filesystem::path& path)
try
{
  const auto canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
  const auto cached = m_Store->meshes.find(canonical);
  if (cached != m_Store->meshes.end()) {
    return cached->second;
  }
  const auto started = std::chrono::steady_clock::now();
  auto mesh = Detail::LoadObj(canonical);
  const auto vertices = mesh.vertices.size();
  const auto triangles = mesh.indices.size() / 3;
  // Reserve the cache entry before registering the asset so a failed operation
  // cannot strand a published resource without its path mapping.
  const auto [entry, inserted] = m_Store->meshes.emplace(canonical, MeshHandle{});
  (void)inserted;
  try {
    entry->second = add<Mesh>(std::move(mesh));
  } catch (...) {
    m_Store->meshes.erase(entry);
    throw;
  }
  const auto handle = entry->second;
  try {
    logger << Log::debug("Loaded mesh: path={} vertices={} triangles={} duration={:.2f}ms",
      path.generic_string(), vertices, triangles,
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
  } catch (...) {
    // Converting a path for diagnostics must not turn a successful load into failure.
  }
  return handle;
}
catch (const std::exception& error) {
  try {
    logger << Log::error("Failed to load mesh: path={} reason={}", path.generic_string(), error.what());
  } catch (...) {
    // Preserve the original load exception even if path conversion fails.
  }
  throw;
}

} // namespace Jade
