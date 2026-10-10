#include "Assets.hpp"

#include "../internal/Mesh/MeshValidation.hpp"
#include "../internal/Mesh/ObjLoader.hpp"

#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace Jade {
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

MeshHandle Assets::LoadMesh(const std::filesystem::path& path)
{
  const auto canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
  const auto cached = m_Store->meshes.find(canonical);
  if (cached != m_Store->meshes.end()) {
    return cached->second;
  }
  auto mesh = Detail::LoadObj(canonical);
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
  return entry->second;
}

} // namespace Jade
