#pragma once
#include <Jade/Graphics/MeshData.hpp>
#include <memory>
#include <utility>
namespace Jade {
namespace Detail {
struct MeshAccess;
}
// Immutable geometry and compiled BLAS. Copies share the complete resource.
class Mesh final {
public:
  Mesh();
  Mesh(const Mesh &) = default;
  Mesh &operator=(const Mesh &) = default;
  static Mesh compile(MeshData data);
  const MeshData &data() const noexcept;

private:
  friend struct Detail::MeshAccess;
  struct Impl;
  explicit Mesh(std::shared_ptr<const Impl> implementation) : m_Impl(std::move(implementation)) {}
  std::shared_ptr<const Impl> m_Impl;
};
} // namespace Jade
