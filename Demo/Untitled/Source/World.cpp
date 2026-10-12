#include "World.hpp"
#include <json.hpp>
#include <fstream>
namespace Untitled {
using namespace Jade;
using Json = nlohmann::json;
namespace {
Vec3 vector(const Json& v) {
  if (!v.is_array() || v.size() != 3) throw std::invalid_argument("World vector needs three values.");
  Vec3 result{v.at(0),v.at(1),v.at(2)};
  if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) throw std::invalid_argument("Nonfinite object state.");
  return result;
}
Json json(Vec3 v) { return Json::array({v.x,v.y,v.z}); }
class Provider final : public ChunkProvider {
  std::filesystem::path root;
public:
  explicit Provider(std::filesystem::path p) : root(std::move(p)) {}
  bool contains(ChunkCoord c) const noexcept override { return c.x >= 0 && c.z >= 0 && c.x < 16 && c.z < 16; }
  ChunkData load(ChunkCoord c) const override {
    if (!contains(c)) throw std::out_of_range("Chunk outside world.");
    std::ifstream file(root/"Chunks"/(std::to_string(c.x)+"_"+std::to_string(c.z)+".json"));
    if (!file) throw std::runtime_error("Missing authored chunk.");
    const auto source = Json::parse(file);
    if (source.at("version") != 1 || source.at("x") != c.x || source.at("z") != c.z) throw std::invalid_argument("Invalid authored chunk.");
    ChunkData data{c,{}};
    for (const auto& o : source.at("objects")) data.objects.push_back({o.at("id"),o.at("type"),o.at("state").dump(),o.at("persistent"),o.at("dynamic")});
    return data;
  }
};
}
void configureWorld(Jade::World& world, Assets& assets, const std::filesystem::path& root,
                    std::shared_ptr<WorldStore> store, JobSystem* jobs) {
  std::ifstream configFile(root/"WorldConfig.json");
  const auto config = Json::parse(configFile);
  if(config.at("version") != 1 || config.at("chunks") != 16 || config.at("chunkSize") != 16 || config.at("cells") != 4)
    throw std::invalid_argument("Unsupported Untitled world configuration.");
  const auto low = assets.add(MeshData::sphere(.6f,8,4));
  const auto high = assets.add(MeshData::sphere(.6f,64,32));
  std::map<std::string,ObjectCodec> codecs;
  codecs["grass"] = {
    [](Jade::World& w, Entity root, const ObjectData& data) {
      const auto state = Json::parse(data.state); const int x = state.at("x"), z = state.at("z");
      root.add<Transform>(Vec3{x*16.f+8,-.1f,z*16.f+8},Quaternion{},Vec3{1,1,1});
      root.add<Collider>(Box{{8,.1f,8}});
      for (int cz=0;cz<4;++cz) for(int cx=0;cx<4;++cx) {
        auto cell=w.create<Archetype<Transform,Geometry,Material>>();
        w.setParent(cell,root);
        cell.get<Transform>().position={cx*4.f-6,.1f,cz*4.f-6};
        cell.get<Geometry>()=Geometry::plane({4,4});
        cell.get<Material>()=Materials::Diffuse((x*4+cx+z*4+cz)%2 ? Renderer::LinearRGB{.22f,.36f,.16f} : Renderer::LinearRGB{.30f,.46f,.22f});
      }
    },
    [](const Jade::World&,Entity) { return std::string("{}"); }
  };
  codecs["ball"] = {
    [low,high](Jade::World&, Entity e, const ObjectData& data) {
      const auto state=Json::parse(data.state);
      if(state.at("version")!=1) throw std::invalid_argument("Unsupported ball state.");
      const int visual=state.at("visual");
      if(visual<0 || visual>2) throw std::invalid_argument("Invalid ball representation.");
      auto& pose=e.add<Transform>();pose.position=vector(state.at("position"));
      const auto rotation=state.at("rotation");pose.rotation=Quaternion{rotation.at(0),rotation.at(1),rotation.at(2),rotation.at(3)}.normalized();
      e.add<Geometry>(visual==1?Geometry::sphere(.6f):Geometry::mesh(visual==0?low:high));
      e.add<Material>(Materials::Diffuse({.72f,.30f,.12f}));
      e.add<Collider>(Sphere{.6f});
      auto& body=e.add<RigidBody>();body.velocity=vector(state.at("velocity"));body.angularVelocity=vector(state.at("angular"));
      body.restitution=.35f;body.linearDamping=.1f;body.collision=Physics::CollisionMode::Continuous;
      e.add<BallVisual>(visual);
    },
    [](const Jade::World&,Entity e) {
      const auto& pose=e.get<Transform>();const auto& body=e.get<RigidBody>();const auto q=pose.rotation;
      return Json{{"version",1},{"visual",e.get<BallVisual>().representation},{"position",json(pose.position)},
        {"rotation",Json::array({q.w,q.x,q.y,q.z})},{"velocity",json(body.velocity)},{"angular",json(body.angularVelocity)}}.dump();
    }
  };
  StreamingSettings settings;settings.world=config.at("id");
  world.streaming().configure(settings,std::make_shared<Provider>(root),std::move(codecs),std::move(store),jobs);
  auto sun=world.create<Archetype<Transform,Light>>();
  sun.get<Transform>().rotation=Quaternion::angleAxis({1,0,0},-55);
  sun.get<Light>().type=LightType::Directional;sun.get<Light>().intensity={3.5f,3.2f,2.8f};
  for (int side : {0,256}) {
    auto x=world.create<Archetype<Transform,Collider>>();x.get<Transform>().position={static_cast<float>(side),2,128};x.get<Collider>()=Collider{Box{{.1f,2,128}}};
    auto z=world.create<Archetype<Transform,Collider>>();z.get<Transform>().position={128,2,static_cast<float>(side)};z.get<Collider>()=Collider{Box{{128,2,.1f}}};
  }
}
}
