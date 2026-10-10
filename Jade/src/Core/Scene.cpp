#include "Scene.hpp"

#include "../internal/Core/Registry.hpp"
#include "../internal/Core/SceneTransforms.hpp"

#include <Jade/Graphics/Camera.hpp>
#include <Jade/Module/FreeCameraControl.hpp>
#include <Jade/Module/FPSCameraControl.hpp>

#include <atomic>
#include <format>
#include <stdexcept>

namespace Jade {
namespace {

std::atomic<std::uint64_t> nextSceneId{1};

} // namespace

Scene::Impl::Impl() : id(nextSceneId.fetch_add(1)), events(std::format("Scene:{}", id)) {}
Scene::Scene() : m_Registry(std::make_shared<Detail::Registry>()), m_Impl(std::make_unique<Impl>()) {}
Scene::~Scene() = default;

void Scene::InstallModule(std::type_index type, std::function<void()> attach)
{
  for (const auto& module : m_Impl->modules) {
    if ((type == typeid(Module::FreeCameraControl) && module.type == typeid(Module::FPSCameraControl)) ||
        (type == typeid(Module::FPSCameraControl) && module.type == typeid(Module::FreeCameraControl))) {
      throw std::logic_error("A Scene may use either FreeCameraControl or FPSCameraControl, not both.");
    }
    if (module.type == type) {
      if (!module.installed) {
        throw std::logic_error("Scene module installation cannot recursively install the same type.");
      }
      return;
    }
  }
  const auto index = m_Impl->modules.size();
  const auto checkpoint = m_Impl->events.SubscriptionCheckpoint();
  m_Impl->modules.push_back({type});
  try {
    attach();
    // Nested installations can reallocate the ledger, so retain only its index.
    m_Impl->modules[index].installed = true;
  } catch (...) {
    // Cancellation invokes no user destructors; pruning remains deferred.
    m_Impl->events.RollbackSubscriptions(checkpoint);
    while (m_Impl->modules.size() > index) m_Impl->modules.pop_back();
    throw;
  }
}

bool Scene::CapturesMouse() const noexcept
{
  for (const auto& module : m_Impl->modules) {
    if (module.installed && module.type == typeid(Module::FPSCameraControl))
      return true;
  }
  return false;
}

bool Scene::owns(Entity entity) const noexcept
{
  return entity.m_Registry.lock() == m_Registry && entity.valid();
}

void Scene::RequireOwned(Entity entity) const
{
  if (entity.m_Registry.lock() != m_Registry) {
    throw std::invalid_argument("Entity belongs to a different Scene.");
  }
  if (!entity.valid()) {
    throw std::logic_error("Entity is no longer alive.");
  }
}

Entity Scene::create()
{
  const auto [index, generation] = m_Registry->Create();
  return Entity(m_Registry, index, generation);
}

void Scene::destroy(Entity entity)
{
  RequireOwned(entity);
  m_Registry->Destroy(entity.m_Index, entity.m_Generation);
  if (!m_Impl->camera.valid()) m_Impl->camera = {};
}

void Scene::setParent(Entity child, Entity parent)
{
  if (!owns(child) || (parent.id() != 0 && !owns(parent))) {
    throw std::invalid_argument("Parenting requires live entities in the same Scene.");
  }
  m_Registry->SetParent(child.m_Index, parent.id() == 0 ? std::nullopt : std::optional(parent.m_Index));
}

Entity Scene::parent(Entity entity) const
{
  RequireOwned(entity);
  const auto parent = m_Registry->Parent(entity.m_Index);
  return parent ? Entity(m_Registry, parent->first, parent->second) : Entity{};
}

std::vector<Entity> Scene::children(Entity entity) const
{
  RequireOwned(entity);
  std::vector<Entity> result;
  for (const auto [index, generation] : m_Registry->Children(entity.m_Index)) {
    result.push_back(Entity(m_Registry, index, generation));
  }
  return result;
}

std::vector<Entity> Scene::Snapshot(std::initializer_list<std::type_index> types) const
{
  std::vector<Entity> result;
  for (const auto [index, generation] : m_Registry->Matching(types)) {
    result.push_back(Entity(m_Registry, index, generation));
  }
  return result;
}

Matrix4 Scene::worldMatrix(Entity entity) const
{
  RequireOwned(entity);
  return Detail::SceneTransforms(*this).Resolve(entity).model;
}

Entity Scene::camera() const noexcept { return m_Impl->camera; }
void Scene::setCamera(Entity camera)
{
  if (camera.id() != 0 && (!owns(camera) || !camera.has<Transform>() || !camera.has<Camera>())) {
    throw std::invalid_argument("Active camera requires Transform and Camera in this Scene.");
  }
  m_Impl->camera = camera;
}

EventConnection Scene::Connect(KeyAction action, KeyChord chord, std::function<EventFlow(const KeyEvent&)> handler)
{
  return EventConnection(m_Impl->events.Connect(action, chord, std::move(handler)));
}

EventConnection Scene::Connect(MouseAction action, MouseButton button, std::function<EventFlow(const MouseEvent&)> handler)
{
  return EventConnection(m_Impl->events.Connect(action, button, std::move(handler)));
}

EventConnection Scene::Connect(std::function<EventFlow(const FrameUpdate&)> handler)
{
  return EventConnection(m_Impl->events.Connect(std::move(handler)));
}

EventConnection Scene::Connect(std::function<EventFlow(const MouseMoved&)> handler)
{
  return EventConnection(m_Impl->events.Connect(std::move(handler)));
}

EventConnection Scene::Connect(std::function<EventFlow(const MouseScrolled&)> handler)
{
  return EventConnection(m_Impl->events.Connect(std::move(handler)));
}

EventConnection Scene::ConnectLifecycle(bool activated, std::function<EventFlow()> handler)
{
  return EventConnection(m_Impl->events.ConnectLifecycle(activated, std::move(handler)));
}

} // namespace Jade
