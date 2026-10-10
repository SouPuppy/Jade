#pragma once

#include <cstdint>
#include <memory>

namespace Jade {
namespace Detail { class AssetStore; }
class Assets;
class Mesh;

// Identity within one Assets owner. Keeping a handle does not keep resources alive.
template<class T>
class AssetHandle final {
public:
  AssetHandle() = default;
  bool valid() const noexcept { return m_Id != 0 && !m_Store.expired(); }
  bool empty() const noexcept { return m_Id == 0; }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(const AssetHandle& a, const AssetHandle& b) noexcept
  {
    return a.m_Id == b.m_Id && !a.m_Store.owner_before(b.m_Store) && !b.m_Store.owner_before(a.m_Store);
  }

private:
  friend class Assets;
  AssetHandle(const std::shared_ptr<Detail::AssetStore>& store, std::uint64_t id) : m_Store(store), m_Id(id) {}
  std::weak_ptr<Detail::AssetStore> m_Store;
  std::uint64_t m_Id = 0;
};

using MeshHandle = AssetHandle<Mesh>;

} // namespace Jade
