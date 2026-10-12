#pragma once

#include <Jade/World/World.hpp>
#include <Jade/Core/Transform.hpp>

#include "../Math/Matrix4.hpp"

#include <unordered_map>
#include <vector>

namespace Jade::Detail {

struct WorldPose {
  Matrix4 model = Matrix4::Identity();
  Matrix4 normal = Matrix4::Identity();
  Quaternion rotation;
};

// Fresh per render/query because component TRS is directly editable.
class WorldTransforms final {
public:
  explicit WorldTransforms(const World& world) : m_World(world) {}

  const WorldPose& Resolve(Entity entity)
  {
    std::vector<Entity> path;
    for (auto cursor = entity; cursor && !m_Poses.contains(cursor.id()); cursor = m_World.parent(cursor)) {
      path.push_back(cursor);
    }
    for (auto cursor = path.rbegin(); cursor != path.rend(); ++cursor) {
      WorldPose pose;
      const auto parent = m_World.parent(*cursor);
      if (parent) pose = m_Poses.at(parent.id());
      if (cursor->has<Transform>()) {
        const auto& local = cursor->get<Transform>();
        pose.model = pose.model * ModelMatrix(local);
        pose.normal = pose.normal * NormalMatrix(local);
        pose.rotation = (pose.rotation * local.rotation.normalized()).normalized();
      }
      m_Poses.emplace(cursor->id(), pose);
    }
    return m_Poses.at(entity.id());
  }

  Matrix4 CameraView(Entity camera)
  {
    const auto& local = camera.get<Transform>();
    WorldPose parentPose;
    if (const auto parent = m_World.parent(camera)) parentPose = Resolve(parent);
    // Own scale is irrelevant to the eye. Ancestor scale affects its position.
    Transform rigid = local;
    rigid.scale = {1, 1, 1};
    const auto world = parentPose.model * ModelMatrix(rigid);
    rigid.position = {world.at(0, 3), world.at(1, 3), world.at(2, 3)};
    rigid.rotation = (parentPose.rotation * local.rotation.normalized()).normalized();
    return ViewMatrix(rigid);
  }

private:
  const World& m_World;
  std::unordered_map<std::uint64_t, WorldPose> m_Poses;
};

} // namespace Jade::Detail
