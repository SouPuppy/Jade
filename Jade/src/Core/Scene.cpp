#include "Scene.hpp"

#include "../internal/Core/Registry.hpp"
#include "../internal/Core/WorldTransforms.hpp"

#include <Jade/Graphics/Camera.hpp>
#include <Jade/Graphics/CameraControl.hpp>

#include <atomic>
#include <format>
#include <stdexcept>

namespace Jade {
namespace {

std::atomic<std::uint64_t> nextSceneId{1};

} // namespace

Scene::Impl::Impl() : id(nextSceneId.fetch_add(1)), events(std::format("Scene:{}", id)) {}
Scene::Scene(std::shared_ptr<World> world) : m_World(std::move(world)), m_Impl(std::make_unique<Impl>()) {
  if (!m_World) throw std::invalid_argument("Scene requires a World.");
}
Scene::~Scene() = default;
void Scene::InstallModule(std::type_index type, std::function<void()> attach)
{
  for (const auto& module : m_Impl->modules) {
    if (module.type == type) {
      if (!module.installed) {
        throw std::logic_error("Scene module installation cannot recursively install the same type.");
      }
      return;
    }
  }
  const auto index = m_Impl->modules.size();
  const auto checkpoint = m_Impl->events.SubscriptionCheckpoint();
  const auto navigation = m_Impl->cameraNavigation;
  m_Impl->modules.push_back({type});
  try {
    attach();
    // Nested installations can reallocate the ledger, so retain only its index.
    m_Impl->modules[index].installed = true;
  } catch (...) {
    // Cancellation invokes no user destructors; pruning remains deferred.
    m_Impl->events.RollbackSubscriptions(checkpoint);
    m_Impl->cameraNavigation = navigation;
    while (m_Impl->modules.size() > index) m_Impl->modules.pop_back();
    throw;
  }
}

bool Scene::CapturesMouse() const noexcept
{
  const auto selected = camera();
  return m_Impl->cameraNavigation && selected.has<Transform>() && selected.has<CameraControl>() &&
    selected.get<CameraControl>().viewMode == ViewMode::FirstPerson;
}

Entity Scene::camera() const noexcept { return m_Impl->camera; }
void Scene::setCamera(Entity camera)
{
  if (camera.id() != 0 && (!world().owns(camera) || !camera.has<Transform>() || !camera.has<Camera>())) {
    throw std::invalid_argument("Active camera requires Transform and Camera in this Scene.");
  }
  if (m_Impl->camera != camera) {
    m_Impl->camera = camera;
    if (m_Impl->cameraNavigation) m_Impl->cameraNavigation->reset();
  }
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
