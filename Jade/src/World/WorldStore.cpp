#include <Jade/World/Streaming.hpp>
#include <json.hpp>
#include <fstream>
#include <chrono>
#include <Windows.h>

namespace Jade {
using Json = nlohmann::json;
namespace {
Json read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot read world save: " + path.string());
  return Json::parse(file);
}
void write(const std::filesystem::path& path, const Json& data) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.exceptions(std::ios::failbit | std::ios::badbit);
  file << data.dump(2); file.flush(); file.close();
  const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot flush world save.");
  const auto ok = FlushFileBuffers(handle); CloseHandle(handle);
  if (!ok) throw std::runtime_error("Cannot flush world save.");
}
}
FileWorldStore::FileWorldStore(std::filesystem::path directory) : m_Directory(std::move(directory)) {}
WorldState FileWorldStore::load(const std::string& world) const {
  WorldState result{world, {}};
  const auto manifest = m_Directory / "commit.json";
  if (!std::filesystem::exists(manifest)) return result;
  const auto commit = read(manifest);
  if (commit.at("version") != 1 || commit.at("world") != world) throw std::runtime_error("Incompatible world save.");
  const auto filename = commit.at("snapshot").get<std::string>();
  if (std::filesystem::path(filename).filename() != filename || !filename.starts_with("snapshot-") || !filename.ends_with(".json"))
    throw std::runtime_error("Invalid world snapshot path.");
  const auto data = read(m_Directory / filename);
  if (data.at("version") != 1 || data.at("world") != world) throw std::runtime_error("Incompatible world snapshot.");
  for (const auto& item : data.at("objects")) {
    ObjectState state;
    state.coord = {item.at("x"), item.at("z")};
    state.data = {item.at("id"), item.at("type"), item.at("state").dump(), true, item.at("dynamic")};
    state.deleted = item.at("deleted");
    if (!state.data.id || !result.objects.emplace(state.data.id, std::move(state)).second) throw std::runtime_error("Duplicate persistent identity.");
  }
  return result;
}
void FileWorldStore::save(const WorldState& state) {
  std::filesystem::create_directories(m_Directory);
  Json data{{"version", 1}, {"world", state.world}, {"objects", Json::array()}};
  for (const auto& [id, object] : state.objects)
    data["objects"].push_back({{"id", id}, {"x", object.coord.x}, {"z", object.coord.z},
      {"type", object.data.type}, {"state", Json::parse(object.data.state)}, {"dynamic", object.data.dynamic}, {"deleted", object.deleted}});
  // One snapshot contains source tombstones and destination ownership together.
  const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto filename = "snapshot-" + std::to_string(serial) + ".json";
  std::string previous;
  if (std::filesystem::exists(m_Directory / "commit.json")) previous = read(m_Directory / "commit.json").at("snapshot");
  write(m_Directory / filename, data);
  const auto pending = m_Directory / "commit.pending";
  write(pending, Json{{"version", 1}, {"world", state.world}, {"snapshot", filename}});
  if (!MoveFileExW(pending.c_str(), (m_Directory / "commit.json").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Cannot commit world snapshot.");
  // Keep the current and previous generations. Cleanup is optional after commit;
  // failure here must not turn an acknowledged save into a failed transaction.
  std::error_code error;
  for (std::filesystem::directory_iterator it(m_Directory,error), end; !error && it != end; it.increment(error)) {
    const auto name = it->path().filename().string();
    if (name != filename && name != previous && name.starts_with("snapshot-") && name.ends_with(".json")) {
      std::error_code ignored; std::filesystem::remove(it->path(),ignored);
    }
  }
}
}
