#pragma once

#include <Jade/Application.hpp>
#include <Jade/Core/Transform.hpp>
#include <Jade/Graphics/Camera.hpp>
#include <Jade/Graphics/Motion.hpp>

// Explicit setup for tests which exercise rendering or camera input.
inline std::shared_ptr<Jade::Scene> MakeTestScene(Jade::Application& app)
{
  auto scene = std::make_shared<Jade::Scene>();
  const auto camera = scene->create();
  auto& pose = camera.add<Jade::Transform>();
  pose.position = {3, 3, 5};
  Jade::Motion::lookAt(pose, {});
  camera.add<Jade::Camera>();
  scene->setCamera(camera);
  app.setScene(scene);
  return scene;
}
