#include "ObjLoader.hpp"

#include "MeshValidation.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Jade::Detail {
namespace {

using Normal = std::array<double, 3>;

Vec3 UnitNormal(Normal value)
{
  const double length = std::hypot(value[0], value[1], value[2]);
  if (length == 0) {
    return {};
  }
  return {static_cast<float>(value[0] / length), static_cast<float>(value[1] / length),
    static_cast<float>(value[2] / length)};
}

class ObjReader {
public:
  explicit ObjReader(const std::filesystem::path& path) : m_Path(path) {}

  Mesh Read()
  {
    std::ifstream input(m_Path);
    input.imbue(std::locale::classic());
    if (!input) {
      Fail("cannot open file");
    }
    std::string line;
    while (std::getline(input, line)) {
      ++m_Line;
      line.erase(line.find('#') == std::string::npos ? line.size() : line.find('#'));
      std::istringstream fields(line);
      fields.imbue(std::locale::classic());
      std::string kind;
      fields >> kind;
      if (kind == "v") {
        if (m_Positions.size() >= std::numeric_limits<std::uint32_t>::max()) {
          Fail("too many vertices");
        }
        m_Positions.push_back(ReadVector(fields));
      } else if (kind == "vn") {
        const auto value = ReadVector(fields);
        const auto normal = UnitNormal({value.x, value.y, value.z});
        if (normal == Vec3{}) {
          Fail("normal must have nonzero length");
        }
        m_Normals.push_back(normal);
      } else if (kind == "vt") {
        // Texture coordinates are validated for face references, then discarded.
        double coordinate = 0;
        if (!(fields >> coordinate) || !std::isfinite(coordinate)) {
          Fail("invalid texture coordinate");
        }
        ++m_Texcoords;
      } else if (kind == "f") {
        ReadFace(fields);
      }
      // Group names, smoothing groups and material directives do not alter geometry.
    }
    if (input.bad()) {
      Fail("file read failed");
    }
    if (m_Mesh.indices.empty()) {
      Fail("no triangle faces found");
    }
    GenerateMissingNormals();
    ValidateMesh(m_Mesh);
    return std::move(m_Mesh);
  }

private:
  [[noreturn]] void Fail(const std::string& reason) const
  {
    throw std::runtime_error("OBJ '" + m_Path.generic_string() + "' line "
      + std::to_string(m_Line) + ": " + reason);
  }

  Vec3 ReadVector(std::istringstream& fields) const
  {
    Vec3 value;
    if (!(fields >> value.x >> value.y >> value.z) || !std::isfinite(value.x)
        || !std::isfinite(value.y) || !std::isfinite(value.z)) {
      Fail("expected three finite coordinates");
    }
    return value;
  }

  std::size_t Resolve(std::string_view text, std::size_t count) const
  {
    std::int64_t index = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), index);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()
        || index == 0) {
      Fail("invalid face index");
    }
    const auto total = static_cast<std::int64_t>(count);
    if (index < -total || index > total) {
      Fail("face index outside its vertex/normal/texture array");
    }
    return static_cast<std::size_t>(index > 0 ? index - 1 : total + index);
  }

  std::uint32_t FaceVertex(const std::string& token)
  {
    const std::string_view text(token);
    const auto firstSlash = text.find('/');
    const auto position = Resolve(text.substr(0, firstSlash), m_Positions.size());
    std::int64_t normal = -1;
    if (firstSlash != std::string_view::npos) {
      const auto secondSlash = text.find('/', firstSlash + 1);
      const auto texture = text.substr(firstSlash + 1,
        secondSlash == std::string_view::npos ? secondSlash : secondSlash - firstSlash - 1);
      if (!texture.empty()) {
        Resolve(texture, m_Texcoords);
      } else if (secondSlash == std::string_view::npos) {
        Fail("missing texture index after '/' ");
      }
      if (secondSlash != std::string_view::npos) {
        normal = static_cast<std::int64_t>(Resolve(text.substr(secondSlash + 1), m_Normals.size()));
      }
    }
    // A position with different normals must remain separate at a hard edge.
    const auto key = std::pair{position, normal};
    const auto found = m_VertexLookup.find(key);
    if (found != m_VertexLookup.end()) {
      return found->second;
    }
    if (m_Mesh.vertices.size() >= std::numeric_limits<std::uint32_t>::max()) {
      Fail("too many indexed vertices");
    }
    const auto index = static_cast<std::uint32_t>(m_Mesh.vertices.size());
    m_Mesh.vertices.push_back({m_Positions[position],
      normal >= 0 ? m_Normals[static_cast<std::size_t>(normal)] : Vec3{}});
    m_SourcePositions.push_back(position);
    m_MissingNormals.push_back(normal < 0);
    m_VertexLookup.emplace(key, index);
    return index;
  }

  void ReadFace(std::istringstream& fields)
  {
    std::vector<std::uint32_t> face;
    std::string token;
    while (fields >> token) {
      face.push_back(FaceVertex(token));
    }
    if (face.size() < 3) {
      Fail("a face needs at least three vertices");
    }
    for (std::size_t corner = 1; corner + 1 < face.size(); ++corner) {
      if (m_Mesh.indices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) - 3) {
        Fail("too many triangle indices");
      }
      m_Mesh.indices.insert(m_Mesh.indices.end(), {face[0], face[corner], face[corner + 1]});
    }
  }

  void GenerateMissingNormals()
  {
    std::vector<Normal> sums(m_Positions.size());
    for (std::size_t triangle = 0; triangle < m_Mesh.indices.size(); triangle += 3) {
      const auto a = m_Mesh.vertices[m_Mesh.indices[triangle]].position;
      const auto b = m_Mesh.vertices[m_Mesh.indices[triangle + 1]].position;
      const auto c = m_Mesh.vertices[m_Mesh.indices[triangle + 2]].position;
      const Normal ab{static_cast<double>(b.x) - a.x, static_cast<double>(b.y) - a.y,
        static_cast<double>(b.z) - a.z};
      const Normal ac{static_cast<double>(c.x) - a.x, static_cast<double>(c.y) - a.y,
        static_cast<double>(c.z) - a.z};
      const Normal weighted{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
        ab[0] * ac[1] - ab[1] * ac[0]};
      for (std::size_t corner = 0; corner < 3; ++corner) {
        auto& sum = sums[m_SourcePositions[m_Mesh.indices[triangle + corner]]];
        for (std::size_t axis = 0; axis < 3; ++axis) {
          sum[axis] += weighted[axis];
        }
      }
    }
    for (std::size_t vertex = 0; vertex < m_Mesh.vertices.size(); ++vertex) {
      if (m_MissingNormals[vertex]) {
        m_Mesh.vertices[vertex].normal = UnitNormal(sums[m_SourcePositions[vertex]]);
      }
    }
  }

  const std::filesystem::path& m_Path;
  std::size_t m_Line = 0;
  std::size_t m_Texcoords = 0;
  std::vector<Vec3> m_Positions;
  std::vector<Vec3> m_Normals;
  std::map<std::pair<std::size_t, std::int64_t>, std::uint32_t> m_VertexLookup;
  std::vector<std::size_t> m_SourcePositions;
  std::vector<bool> m_MissingNormals;
  Mesh m_Mesh;
};

} // namespace

Mesh LoadObj(const std::filesystem::path& path)
{
  return ObjReader(path).Read();
}

} // namespace Jade::Detail
