#pragma once

#include <Jade/Assets/AssetHandle.hpp>
#include <Jade/Graphics/Mesh.hpp>

#include <filesystem>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <utility>

namespace Jade {

// Synchronous, main-thread owner of shared immutable CPU resources.
class Assets final {
public:
  Assets();
  ~Assets();
  Assets(const Assets&) = delete;
  Assets& operator=(const Assets&) = delete;

  template<class T> AssetHandle<T> add(T resource)
  {
    static_assert(std::is_same_v<T, std::remove_cvref_t<T>>, "Assets require unqualified object types.");
    if constexpr (std::is_same_v<T, Mesh>) {
      ValidateMesh(resource);
    }
    auto stored = std::make_shared<const T>(std::move(resource));
    return AssetHandle<T>(m_Store, Add(typeid(T), std::move(stored)));
  }

  template<class T> const T& get(AssetHandle<T> handle) const
  {
    return *static_cast<const T*>(Get(handle.m_Store, handle.m_Id, typeid(T)));
  }

  // Wavefront OBJ is the only file loader in this version. Relative paths use
  // the working directory. Repeated canonical paths share the same resource.
  template<class T> AssetHandle<T> load(const std::filesystem::path& path)
  {
    static_assert(std::is_same_v<T, Mesh>, "Assets::load currently supports Mesh OBJ resources only.");
    return LoadMesh(path);
  }

private:
  static void ValidateMesh(const Mesh& mesh);
  std::uint64_t Add(std::type_index type, std::shared_ptr<const void> resource);
  const void* Get(const std::weak_ptr<Detail::AssetStore>& owner, std::uint64_t id, std::type_index type) const;
  MeshHandle LoadMesh(const std::filesystem::path& path);
  std::shared_ptr<Detail::AssetStore> m_Store;
};

} // namespace Jade
