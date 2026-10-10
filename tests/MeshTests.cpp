#include <Jade/Graphics/Mesh.hpp>
#include <Jade/Assets/Assets.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace {

class ObjFixture {
public:
  ObjFixture()
  {
    static std::atomic<unsigned int> sequence = 0;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    m_Directory = std::filesystem::temp_directory_path() /
                  ("jade-mesh-test-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
    if (!std::filesystem::create_directory(m_Directory)) {
      throw std::runtime_error("Could not create an isolated OBJ test directory.");
    }
  }
  ~ObjFixture()
  {
    std::error_code error;
    std::filesystem::remove_all(m_Directory, error);
  }
  std::filesystem::path Write(const std::string &contents) const
  {
    const auto path = m_Directory / "mesh.obj";
    std::ofstream output(path);
    output << contents;
    output.close();
    if (!output) {
      throw std::runtime_error("Could not write the OBJ test fixture.");
    }
    return path;
  }
  const std::filesystem::path &Directory() const { return m_Directory; }

private:
  std::filesystem::path m_Directory;
};

Jade::MeshData LoadMesh(const std::filesystem::path &path)
{
  Jade::Assets assets;
  return assets.get(assets.load<Jade::Mesh>(path)).data();
}

void ExpectUnit(Jade::Vec3 normal) { EXPECT_NEAR(Jade::dot(normal, normal), 1.0f, 1e-5f); }

} // namespace

TEST(Mesh, CubeHasRequestedBoundsOutwardWindingAndHardFaceNormals)
{
  const auto mesh = Jade::MeshData::cube(2);
  ASSERT_EQ(mesh.vertices.size(), 24u);
  ASSERT_EQ(mesh.indices.size(), 36u);
  for (const auto &vertex : mesh.vertices) {
    EXPECT_FLOAT_EQ(std::abs(vertex.position.x), 1);
    EXPECT_FLOAT_EQ(std::abs(vertex.position.y), 1);
    EXPECT_FLOAT_EQ(std::abs(vertex.position.z), 1);
    ExpectUnit(vertex.normal);
    EXPECT_FLOAT_EQ(Jade::dot(vertex.position, vertex.normal), 1);
  }
  for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
    const auto &a = mesh.vertices[mesh.indices[triangle]];
    const auto &b = mesh.vertices[mesh.indices[triangle + 1]];
    const auto &c = mesh.vertices[mesh.indices[triangle + 2]];
    EXPECT_EQ(a.normal, b.normal);
    EXPECT_EQ(a.normal, c.normal);
    EXPECT_GT(Jade::dot(Jade::cross(b.position - a.position, c.position - a.position), a.normal), 0);
  }
  EXPECT_FLOAT_EQ(Jade::MeshData::cube().vertices[0].position.z, 0.5f);
  for (const auto size :
       {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    EXPECT_THROW(Jade::MeshData::cube(size), std::invalid_argument);
  }
}

TEST(Mesh, SphereHasRequestedRadiusAndSmoothOutwardNormals)
{
  const auto mesh = Jade::MeshData::sphere(2.5f);
  ASSERT_EQ(mesh.vertices.size(), 482u);
  ASSERT_EQ(mesh.indices.size(), 2880u);
  EXPECT_EQ(mesh.vertices.front().position, (Jade::Vec3{0, 2.5f, 0}));
  EXPECT_EQ(mesh.vertices.back().position, (Jade::Vec3{0, -2.5f, 0}));
  for (const auto &vertex : mesh.vertices) {
    ExpectUnit(vertex.normal);
    EXPECT_NEAR(Jade::dot(vertex.position, vertex.position), 6.25f, 1e-5f);
    EXPECT_EQ(vertex.position, vertex.normal * 2.5f);
  }
  EXPECT_FLOAT_EQ(Jade::MeshData::sphere().vertices.front().position.y, 0.5f);
  Jade::Assets assets;
  const auto handle = assets.add(mesh);
  EXPECT_EQ(assets.get(handle).data().indices, mesh.indices);
}

TEST(Mesh, SphereIsClosedWithNondegenerateOutwardTrianglesAtPolesAndSeam)
{
  for (const auto [longitude, latitude] : {std::pair{3u, 2u}, {7u, 5u}, {32u, 16u}}) {
    const auto mesh = Jade::MeshData::sphere(1, longitude, latitude);
    EXPECT_EQ(mesh.vertices.size(), 2u + longitude * (latitude - 1));
    EXPECT_EQ(mesh.indices.size(), 6u * longitude * (latitude - 1));
    struct Edge {
      int count = 0;
      int direction = 0;
    };
    std::map<std::pair<std::uint32_t, std::uint32_t>, Edge> edges;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
      const auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
      ASSERT_LT(a, mesh.vertices.size());
      ASSERT_LT(b, mesh.vertices.size());
      ASSERT_LT(c, mesh.vertices.size());
      const auto pa = mesh.vertices[a].position, pb = mesh.vertices[b].position,
                 pc = mesh.vertices[c].position;
      EXPECT_GT(Jade::dot(Jade::cross(pb - pa, pc - pa), pa + pb + pc), 0);
      for (const auto [from, to] : {std::pair{a, b}, {b, c}, {c, a}}) {
        auto &edge = edges[{std::min(from, to), std::max(from, to)}];
        ++edge.count;
        edge.direction += from < to ? 1 : -1;
      }
    }
    for (const auto &[key, edge] : edges) {
      (void)key;
      EXPECT_EQ(edge.count, 2);
      EXPECT_EQ(edge.direction, 0);
    }
    // Euler characteristic of a closed sphere; every vertex participates.
    EXPECT_EQ(mesh.vertices.size() + mesh.indices.size() / 3, edges.size() + 2);
  }
}

TEST(Mesh, SphereRejectsInvalidRadiusSubdivisionsAndUnrepresentableCounts)
{
  for (const auto radius :
       {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    EXPECT_THROW(Jade::MeshData::sphere(radius), std::invalid_argument);
  }
  EXPECT_THROW(Jade::MeshData::sphere(1, 0, 16), std::invalid_argument);
  EXPECT_THROW(Jade::MeshData::sphere(1, 2, 16), std::invalid_argument);
  EXPECT_THROW(Jade::MeshData::sphere(1, 32, 0), std::invalid_argument);
  EXPECT_THROW(Jade::MeshData::sphere(1, 32, 1), std::invalid_argument);
  const auto max = std::numeric_limits<std::uint32_t>::max();
  EXPECT_THROW(Jade::MeshData::sphere(1, max, 2), std::invalid_argument);
  EXPECT_THROW(Jade::MeshData::sphere(1, 3, max), std::invalid_argument);
  EXPECT_THROW(Jade::MeshData::sphere(1, max, max), std::invalid_argument);
}

TEST(Mesh, TriangleBuilderUsesVec3AndRequiresCompletePrimitives)
{
  Jade::Assets assets;
  auto triangle = Jade::MeshData::triangle({0, 0, 2}, {1, 0, 2}, {0, 1, 2});
  EXPECT_EQ(triangle.vertices[0].normal, (Jade::Vec3{0, 0, 1}));
  EXPECT_NO_THROW(assets.add(triangle));
  triangle.indices.push_back(0);
  EXPECT_THROW(assets.add(triangle), std::invalid_argument);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(Jade::MeshData::triangle({}, {1, 1, 1}, {0, nan, 0}), std::invalid_argument);
}

TEST(Assets, FailedObjLoadsCanBeRetriedAndCachedLoadsRemainImmutable)
{
  ObjFixture fixture;
  Jade::Assets assets;
  const auto path = fixture.Write("v 0 0 0\nf 0 1 2\n");
  EXPECT_THROW(assets.load<Jade::Mesh>(path), std::runtime_error);
  fixture.Write("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  const auto loaded = assets.load<Jade::Mesh>(path);
  EXPECT_EQ(assets.get(loaded).data().vertices.size(), 3u);
  fixture.Write("invalid replacement\n");
  EXPECT_EQ(assets.load<Jade::Mesh>(path), loaded);
  EXPECT_EQ(assets.get(loaded).data().vertices.size(), 3u);
}

TEST(Assets, OwnsMeshSnapshotsAndRejectsInvalidRegistration)
{
  Jade::Assets assets;
  auto mesh = Jade::MeshData::cube(2);
  const auto handle = assets.add(mesh);
  mesh.vertices[0].position = {100, 100, 100};
  EXPECT_EQ(assets.get(handle).data().vertices[0].position, (Jade::Vec3{-1, -1, 1}));
  for (int kind = 0; kind < 4; ++kind) {
    auto invalid = Jade::MeshData::cube();
    if (kind == 0) {
      invalid.indices[0] = 24;
    } else if (kind == 1) {
      invalid.indices.pop_back();
    } else if (kind == 2) {
      invalid.vertices[0].position.x = std::numeric_limits<float>::infinity();
    } else {
      invalid.vertices[0].normal.y = std::numeric_limits<float>::quiet_NaN();
    }
    EXPECT_THROW(assets.add(std::move(invalid)), std::invalid_argument);
    EXPECT_EQ(assets.get(handle).data().vertices[0].position, (Jade::Vec3{-1, -1, 1}));
    EXPECT_EQ(assets.get(handle).data().indices.size(), 36u);
  }
  const auto empty = assets.add(Jade::Mesh{});
  EXPECT_TRUE(assets.get(empty).data().vertices.empty());
  EXPECT_TRUE(assets.get(empty).data().indices.empty());
}

TEST(Mesh, ObjGeneratesAreaWeightedNormalsAndSharesPositions)
{
  ObjFixture fixture;
  const auto mesh = LoadMesh(fixture.Write("v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 2\nf 1 2 3\nf 1 4 2\n"));
  ASSERT_EQ(mesh.vertices.size(), 4u);
  ASSERT_EQ(mesh.indices.size(), 6u);
  EXPECT_EQ(mesh.indices[0], mesh.indices[3]);
  EXPECT_EQ(mesh.indices[1], mesh.indices[5]);
  const auto normal = mesh.vertices[0].normal;
  EXPECT_FLOAT_EQ(normal.x, 0);
  EXPECT_NEAR(normal.y, 2.0f / std::sqrt(5.0f), 1e-6f);
  EXPECT_NEAR(normal.z, 1.0f / std::sqrt(5.0f), 1e-6f);
  for (const auto &vertex : mesh.vertices) {
    ExpectUnit(vertex.normal);
  }
}

TEST(Mesh, ObjPreservesExplicitNormalsAndSplitsHardEdges)
{
  ObjFixture fixture;
  const auto mesh = LoadMesh(fixture.Write("v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 4\nvn 0 0 -2\n"
                                           "f 1//1 2//1 3//1\nf 1//2 3//2 2//2\n"));
  ASSERT_EQ(mesh.vertices.size(), 6u);
  ASSERT_EQ(mesh.indices.size(), 6u);
  for (std::size_t index = 0; index < 3; ++index) {
    EXPECT_EQ(mesh.vertices[index].normal, (Jade::Vec3{0, 0, 1}));
    EXPECT_EQ(mesh.vertices[index + 3].normal, (Jade::Vec3{0, 0, -1}));
  }
  EXPECT_EQ(mesh.vertices[0].position, mesh.vertices[3].position);
}

TEST(Mesh, ObjAcceptsNegativeIndicesUvsCommentsAndConvexPolygons)
{
  ObjFixture fixture;
  const auto mesh = LoadMesh(fixture.Write("# unit quad\r\nv 0 0 0\r\nv 1 0 0\r\nv 1 1 0\r\nv 0 1 0\r\n"
                                           "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
                                           "o Quad\ng Front\ns 1\nusemtl Ignored\n"
                                           "f -4/-4/-1 -3/-3/-1 -2/-2/-1 -1/-1/-1 # two triangles\n"));
  ASSERT_EQ(mesh.vertices.size(), 4u);
  ASSERT_EQ(mesh.indices.size(), 6u);
  EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));
  for (const auto &vertex : mesh.vertices) {
    EXPECT_EQ(vertex.normal, (Jade::Vec3{0, 0, 1}));
  }
  const auto noNormals = LoadMesh(fixture.Write("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0\nf 1/1 2/1 3/1\n"));
  for (const auto &vertex : noNormals.vertices) {
    EXPECT_EQ(vertex.normal, (Jade::Vec3{0, 0, 1}));
  }
}

TEST(Mesh, ObjReportsMalformedInputWithPathAndLine)
{
  ObjFixture fixture;
  const std::string positions = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
  for (const auto &invalid : {"f 0 2 3", "f 1 2 4", "f -4 -2 -1", "f 1 2", "f 1x 2 3", "f 1/1 2/1 3/1",
                              "f 1//1 2//1 3//1", "f 1/ 2/ 3/"}) {
    SCOPED_TRACE(invalid);
    const auto path = fixture.Write(positions + invalid + "\n");
    try {
      LoadMesh(path);
      FAIL() << "Accepted malformed OBJ";
    } catch (const std::runtime_error &error) {
      EXPECT_NE(std::string(error.what()).find("mesh.obj' line 4:"), std::string::npos);
    }
  }
  for (const auto &contents : {"", "v 0 0 0\n", "v 1 2\n", "v nan 0 0\n", "v 1e99 0 0\n", "vn 0 0 0\n"}) {
    EXPECT_THROW(LoadMesh(fixture.Write(contents)), std::runtime_error);
  }
  EXPECT_THROW(LoadMesh(fixture.Directory() / "missing.obj"), std::runtime_error);
}

TEST(Mesh, LoadsTheSmallestBundledBunnyAndGeneratesFiniteNormals)
{
  const auto mesh = LoadMesh(std::filesystem::path(JADE_TEST_ASSET_DIR) / "bunny.obj");
  ASSERT_EQ(mesh.vertices.size(), 453u);
  ASSERT_EQ(mesh.indices.size(), 948u * 3);
  std::size_t nonzeroNormals = 0;
  for (const auto &vertex : mesh.vertices) {
    EXPECT_GE(vertex.position.x, -0.094f);
    EXPECT_LE(vertex.position.x, 0.059f);
    EXPECT_GE(vertex.position.y, 0.033f);
    EXPECT_LE(vertex.position.y, 0.182f);
    const auto length = Jade::dot(vertex.normal, vertex.normal);
    EXPECT_TRUE(std::isfinite(length));
    if (length > 0) {
      ExpectUnit(vertex.normal);
      ++nonzeroNormals;
    }
  }
  EXPECT_GT(nonzeroNormals, 440u);
  for (const auto index : mesh.indices) {
    EXPECT_LT(index, mesh.vertices.size());
  }
}
