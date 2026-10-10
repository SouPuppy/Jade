#include <Jade/Application.hpp>
#include <Jade/Objects/Camera.hpp>
#include <Jade/Objects/Object.hpp>
#include <Jade/Geometry/Triangle.hpp>

#include <gtest/gtest.h>

#include <glad/gl.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>

static_assert(std::is_base_of_v<Jade::Object, Jade::Camera>);
static_assert(!std::is_base_of_v<Jade::Object, Jade::Line>);
static_assert(!std::is_base_of_v<Jade::Object, Jade::Triangle>);

TEST(Camera, SharesObjectPositionAndPreservesValidationThroughTheBaseClass)
{
  Jade::Camera camera;
  Jade::Object& object = camera;
  object.setPos({0, 0, 5});
  camera.lookAt({0, 0, 0});
  EXPECT_EQ(camera.Position(), (Jade::Vec3{0, 0, 5}));
  EXPECT_THROW(object.setPos({0, std::numeric_limits<float>::infinity(), 0}), std::invalid_argument);
  EXPECT_EQ(object.Position(), (Jade::Vec3{0, 0, 5}));
  object.setRotation({0, 90, 0});
  EXPECT_THROW(camera.lookAt({0, 0, 5}), std::invalid_argument);
  EXPECT_EQ(camera.Rotation(), (Jade::Vec3{0, 90, 0}));
  camera.lookAt({0, 0, 0});
  EXPECT_EQ(camera.Rotation(), (Jade::Vec3{}));
}

TEST(Camera, RejectsInvalidChangesWithoutReplacingState)
{
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float infinity = std::numeric_limits<float>::infinity();
  Jade::Camera camera;
  camera.setPos({0, 0, 5});
  camera.lookAt({0, 0, 0});
  EXPECT_THROW(camera.setPos({nan, 0, 5}), std::invalid_argument);
  EXPECT_THROW(camera.lookAt({0, 0, 0}, {0, 0, 1}), std::invalid_argument);
  EXPECT_THROW(camera.lookAt({1, 2, 3}, {0, 0, 0}), std::invalid_argument);
  EXPECT_EQ(camera.Position(), (Jade::Vec3{0, 0, 5}));
  EXPECT_EQ(camera.Target(), (Jade::Vec3{0, 0, 0}));
  EXPECT_EQ(camera.Up(), (Jade::Vec3{0, 1, 0}));

  camera.setOrthographic(8, 0.5f, 40);
  for (float fov : {0.0f, -1.0f, 180.0f, nan, infinity}) {
    EXPECT_THROW(camera.setPerspective(fov, 1, 20), std::invalid_argument);
  }
  EXPECT_THROW(camera.setPerspective(60, 10, 5), std::invalid_argument);
  EXPECT_THROW(camera.setOrthographic(0), std::invalid_argument);
  EXPECT_THROW(camera.setOrthographic(4, 0, 10), std::invalid_argument);
  EXPECT_THROW(camera.setOrthographic(4, 1, infinity), std::invalid_argument);
  EXPECT_EQ(camera.Mode(), Jade::Projection::Orthographic);
  EXPECT_FLOAT_EQ(camera.ViewHeight(), 8);
  EXPECT_FLOAT_EQ(camera.NearPlane(), 0.5f);
  EXPECT_FLOAT_EQ(camera.FarPlane(), 40);
  EXPECT_THROW((Jade::Camera{static_cast<Jade::Projection>(42)}), std::invalid_argument);

  camera.setPerspective(75, 1, 20);
  EXPECT_EQ(camera.Mode(), Jade::Projection::Perspective);
  EXPECT_FLOAT_EQ(camera.FOV(), 75);
}

TEST(Triangle, OwnsVerticesAndRejectsInvalidPositionAndColorChanges)
{
  Jade::Vec2 a{1, 1};
  Jade::Triangle triangle(a, {5, 1}, {1, 5}, {1, 0, 0, 1});
  a.x = 100;
  EXPECT_FLOAT_EQ(triangle.Vertices()[0].x, 1);
  const auto before = triangle.Vertices();
  const float nan = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(triangle.setPos({2, 2}, {nan, 3}, {4, 4}), std::invalid_argument);
  EXPECT_EQ(triangle.Vertices(), before);
  EXPECT_THROW(triangle.setColor({1, -1, 0, 1}), std::invalid_argument);
  EXPECT_FLOAT_EQ(triangle.GetColor().red, 1);
  EXPECT_FLOAT_EQ(triangle.GetColor().green, 0);
}

TEST(Object, ValidatesTransformAndColorWithoutChangingPreviousValues)
{
  Jade::Object cube;
  cube.setMesh(Jade::Mesh::cube(2));
  cube.setPos({1, 2, 3});
  cube.setRotation({10, 20, 30});
  cube.setScale({2, 3, 4});
  const float infinity = std::numeric_limits<float>::infinity();
  EXPECT_THROW(cube.setPos({0, infinity, 0}), std::invalid_argument);
  EXPECT_THROW(cube.setRotation({0, 0, infinity}), std::invalid_argument);
  EXPECT_THROW(cube.setScale({1, 0, 1}), std::invalid_argument);
  EXPECT_THROW(cube.setColor({2, 0, 0, 1}), std::invalid_argument);
  EXPECT_EQ(cube.Position(), (Jade::Vec3{1, 2, 3}));
  EXPECT_EQ(cube.Rotation(), (Jade::Vec3{10, 20, 30}));
  EXPECT_EQ(cube.Scale(), (Jade::Vec3{2, 3, 4}));
  EXPECT_FLOAT_EQ(cube.GetColor().red, 1);
}

TEST(Application, OwnsAllPrimitivesAndUsesTheLatestCreatedCamera)
{
  Jade::ApplicationConfig config;
  config.window.visible = false;
  config.window.width = 160;
  config.window.height = 120;
  config.logFrames = false;
  Jade::Application app(config);
  auto& initial = app.camera();
  auto& camera = app.create<Jade::Camera>(Jade::Projection::Orthographic);
  EXPECT_EQ(&app.camera(), &camera);
  EXPECT_NE(&initial, &camera);
  auto& cube = app.create<Jade::Object>();
  cube.setMesh(Jade::Mesh::cube(2.0f));
  const auto* address = &cube;
  app.create<Jade::Triangle>(Jade::Vec2{1, 1}, Jade::Vec2{3, 1}, Jade::Vec2{1, 3});
  app.create<Jade::Line>(Jade::Vec2{1, 8}, Jade::Vec2{5, 8});
  for (int index = 0; index < 8; ++index) {
    app.create<Jade::Object>().setPos({100, 0, 0});
  }
  EXPECT_EQ(&cube, address);
  app.setWireframe(true);
  EXPECT_NO_THROW(app.Run({2, true}));
  std::array<GLint, 2> polygonMode{};
  glGetIntegerv(GL_POLYGON_MODE, polygonMode.data());
  EXPECT_EQ(polygonMode[0], GL_LINE);
  EXPECT_EQ(polygonMode[1], GL_LINE);
  auto& perspective = app.create<Jade::Camera>();
  EXPECT_EQ(&app.camera(), &perspective);
  EXPECT_EQ(camera.Mode(), Jade::Projection::Orthographic);
  cube.setRotation({20, 30, 0});
  app.setWireframe(false);
  EXPECT_NO_THROW(app.Run({2, true}));
  glGetIntegerv(GL_POLYGON_MODE, polygonMode.data());
  EXPECT_EQ(polygonMode[0], GL_FILL);
  EXPECT_EQ(polygonMode[1], GL_FILL);
}
