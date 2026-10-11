#include <Jade/Renderer/RayTracing/RayTracing.hpp>
#include "TestScene.hpp"
#include <Jade/Jade.hpp>
#include "internal/Math/Matrix4.hpp"

#include <gtest/gtest.h>
#include <glad/gl.h>

#include <array>
#include <limits>
#include <type_traits>

static_assert(std::is_aggregate_v<Jade::Transform>);
static_assert(std::is_aggregate_v<Jade::Camera>);
static_assert(std::is_aggregate_v<Jade::Geometry>);
static_assert(std::is_aggregate_v<Jade::Material>);

TEST(Camera, LookAtRejectsInvalidFramesWithoutReplacingRotation)
{
  Jade::Transform pose;
  pose.position = {0, 0, 5};
  Jade::Motion::yaw(pose, 30);
  const auto rotation = pose.rotation;
  EXPECT_THROW(Jade::Motion::lookAt(pose, pose.position), std::invalid_argument);
  EXPECT_THROW(Jade::Motion::lookAt(pose, {0, 0, 0}, {0, 0, 1}), std::invalid_argument);
  EXPECT_THROW(Jade::Motion::lookAt(pose, {1, 2, 3}, {}), std::invalid_argument);
  EXPECT_EQ(pose.rotation, rotation);
  Jade::Motion::lookAt(pose, {0, 0, 0});
  EXPECT_EQ(Jade::Motion::getFront(pose), (Jade::Vec3{0, 0, -1}));
}

TEST(Components, DataIsValidatedWhenConsumed)
{
  const auto infinity = std::numeric_limits<float>::infinity();
  Jade::Camera lens;
  for (const float fov : {0.0f, -1.0f, 180.0f, infinity}) {
    lens.fovDegrees = fov;
    EXPECT_THROW(Jade::Detail::ProjectionMatrix(lens, 1), std::invalid_argument);
  }
  lens = {};
  lens.farPlane = lens.nearPlane;
  EXPECT_THROW(Jade::Detail::ProjectionMatrix(lens, 1), std::invalid_argument);
  lens = {};
  lens.viewHeight = 0;
  EXPECT_THROW(Jade::Motion::zoom(lens, 1), std::invalid_argument);
  lens = {};
  lens.projection = static_cast<Jade::Projection>(42);
  EXPECT_THROW(Jade::Detail::ProjectionMatrix(lens, 1), std::invalid_argument);
  Jade::Transform pose;
  pose.position.y = infinity;
  EXPECT_THROW(Jade::Detail::ModelMatrix(pose), std::invalid_argument);
  pose = {};
  pose.scale.y = 0;
  EXPECT_THROW(Jade::Detail::ModelMatrix(pose), std::invalid_argument);
  pose = {};
  pose.rotation = {0, 0, 0, 0};
  EXPECT_THROW(Jade::Detail::ViewMatrix(pose), std::invalid_argument);
}

TEST(Application, ExplicitCameraSelectionRejectsForeignAndIncompleteEntities)
{
  Jade::EngineConfig config;
  config.window.visible = false;
  config.logPerformance = false;
  config.window.width = 160;
  config.window.height = 120;
  Jade::Application app(config);
  MakeTestScene(app);
  const auto initial = app.runtime().scene()->camera();
  EXPECT_TRUE(initial.has<Jade::Transform>());
  EXPECT_TRUE(initial.has<Jade::Camera>());
  EXPECT_EQ(initial.get<Jade::Transform>().position, (Jade::Vec3{3, 3, 5}));
  const auto selected = app.runtime().scene()->create();
  selected.add<Jade::Camera>().projection = Jade::Projection::Orthographic;
  EXPECT_EQ(app.runtime().scene()->camera(), initial);
  EXPECT_THROW(app.runtime().scene()->setCamera(selected), std::invalid_argument);
  selected.add<Jade::Transform>().position = {0, 0, 5};
  app.runtime().scene()->setCamera(selected);
  EXPECT_EQ(app.runtime().scene()->camera(), selected);
  Jade::Scene other;
  const auto foreign = other.create();
  foreign.add<Jade::Transform>();
  foreign.add<Jade::Camera>();
  EXPECT_THROW(app.runtime().scene()->setCamera(foreign), std::invalid_argument);
  EXPECT_EQ(app.runtime().scene()->camera(), selected);
  selected.remove<Jade::Camera>();
  EXPECT_NO_THROW(app.Run({2}));
  selected.add<Jade::Camera>();
  EXPECT_NO_THROW(app.Run({1}));
  app.runtime().scene()->destroy(selected);
  EXPECT_FALSE(app.runtime().scene()->camera().valid());
  EXPECT_NO_THROW(app.Run({1}));
  EXPECT_THROW(app.runtime().scene()->setCamera(selected), std::invalid_argument);
  app.runtime().scene()->setCamera(initial);
  app.runtime().renderer().setRayTracingEnabled(false); app.runtime().renderer().setWireframe(true);
  EXPECT_NO_THROW(app.Run({1}));
  std::array<GLint, 2> mode{};
  glGetIntegerv(GL_POLYGON_MODE, mode.data());
  EXPECT_TRUE(app.runtime().renderer().wireframeEnabled());
  EXPECT_EQ(mode[0], GL_FILL);
  app.runtime().renderer().setRayTracingEnabled(true); app.runtime().renderer().setWireframe(false);
  EXPECT_NO_THROW(app.Run({1}));
  glGetIntegerv(GL_POLYGON_MODE, mode.data());
  EXPECT_EQ(mode[0], GL_FILL);
}
