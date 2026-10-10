#include <Jade/Objects/Camera.hpp>
#include <Jade/Objects/Object.hpp>
#include <Jade/Vector.hpp>

#include "internal/Math/Matrix4.hpp"

#include <gtest/gtest.h>

#include <array>
#include <type_traits>

namespace {

std::array<float, 4> Transform(const Jade::Detail::Matrix4& matrix, float x, float y, float z)
{
  const std::array<float, 4> point{x, y, z, 1};
  std::array<float, 4> result{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      result[row] += matrix.at(row, column) * point[column];
    }
  }
  return result;
}

std::array<float, 3> Project(const Jade::Detail::Matrix4& matrix, float x, float y, float z)
{
  const auto clip = Transform(matrix, x, y, z);
  return {clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]};
}

} // namespace

TEST(Vector, TwoDimensionalArithmeticAndSignedCross)
{
  static_assert(std::is_same_v<Jade::Vec2D, Jade::Vec2>);
  constexpr Jade::Vec2 a{1, 2};
  constexpr Jade::Vec2 b{3, 4};
  static_assert(Jade::dot(a, b) == 11);
  EXPECT_EQ(a + b, (Jade::Vec2{4, 6}));
  EXPECT_EQ(a - b, (Jade::Vec2{-2, -2}));
  EXPECT_EQ(-a, (Jade::Vec2{-1, -2}));
  EXPECT_EQ(a * 2.0f, (Jade::Vec2{2, 4}));
  EXPECT_EQ(2.0f * a, a * 2.0f);
  EXPECT_FLOAT_EQ(a.dot(b), 11);
  EXPECT_FLOAT_EQ(Jade::cross(a, b), -2);
  EXPECT_FLOAT_EQ(b.cross(a), 2);
}

TEST(Vector, ThreeDimensionalArithmeticAndOrthogonalCross)
{
  static_assert(std::is_same_v<Jade::Vec3D, Jade::Vec3>);
  constexpr Jade::Vec3 a{1, 2, 3};
  constexpr Jade::Vec3 b{4, 5, 6};
  constexpr auto normal = Jade::cross(a, b);
  static_assert(normal == Jade::Vec3{-3, 6, -3});
  EXPECT_EQ(a + b, (Jade::Vec3{5, 7, 9}));
  EXPECT_EQ(a - b, (Jade::Vec3{-3, -3, -3}));
  EXPECT_EQ(-a, (Jade::Vec3{-1, -2, -3}));
  EXPECT_EQ(a * 2.0f, (Jade::Vec3{2, 4, 6}));
  EXPECT_EQ(2.0f * a, a * 2.0f);
  EXPECT_FLOAT_EQ(a.dot(b), 32);
  EXPECT_FLOAT_EQ(Jade::dot(normal, a), 0);
  EXPECT_FLOAT_EQ(Jade::dot(normal, b), 0);
  EXPECT_EQ(b.cross(a), -normal);
  EXPECT_EQ(Jade::cross(Jade::Vec3{1, 0, 0}, Jade::Vec3{0, 1, 0}), (Jade::Vec3{0, 0, 1}));
}

TEST(Matrix, ViewMapsEyeToOriginAndTargetToNegativeZ)
{
  Jade::Camera camera;
  camera.setPos({0, 0, 5});
  camera.lookAt({0, 0, 0});
  auto view = Jade::Detail::ViewMatrix(camera);
  EXPECT_EQ(Transform(view, 0, 0, 5), (std::array<float, 4>{0, 0, 0, 1}));
  EXPECT_EQ(Transform(view, 0, 0, 0), (std::array<float, 4>{0, 0, -5, 1}));
  camera.setPos({5, 0, 0});
  camera.lookAt({0, 0, 0});
  view = Jade::Detail::ViewMatrix(camera);
  const auto target = Transform(view, 0, 0, 0);
  EXPECT_NEAR(target[2], -5, 1e-6);
  EXPECT_NEAR(Transform(view, 0, 1, 0)[1], 1, 1e-6);
  EXPECT_NEAR(Transform(view, 0, 0, 1)[0], -1, 1e-6);
}

TEST(Matrix, CameraObjectTransformChangesTheViewAndLookAtResetsTheRotation)
{
  Jade::Camera camera;
  camera.setPos({0, 0, 5});
  camera.lookAt({0, 0, 0});
  camera.setRotation({0, 90, 0});
  camera.setScale({2, 1, 2});
  const auto view = Jade::Detail::ViewMatrix(camera);
  const auto ahead = Transform(view, -2, 0, 5);
  EXPECT_NEAR(ahead[0], 0, 1e-6);
  EXPECT_NEAR(ahead[1], 0, 1e-6);
  EXPECT_NEAR(ahead[2], -1, 1e-6);
  EXPECT_EQ(Transform(view, 0, 0, 5), (std::array<float, 4>{0, 0, 0, 1}));
  camera.lookAt({0, 0, 0});
  camera.setScale({1, 1, 1});
  EXPECT_EQ(Transform(Jade::Detail::ViewMatrix(camera), 0, 0, 0),
    (std::array<float, 4>{0, 0, -5, 1}));
}

TEST(Matrix, ProjectionMapsClipPlanesAndRespectsAspectAndDistance)
{
  Jade::Camera camera;
  camera.setPerspective(90, 1, 11);
  auto projection = Jade::Detail::ProjectionMatrix(camera, 2);
  EXPECT_NEAR(Project(projection, 0, 0, -1)[2], -1, 1e-6);
  EXPECT_NEAR(Project(projection, 0, 0, -11)[2], 1, 1e-6);
  EXPECT_NEAR(Project(projection, 2, 1, -1)[0], 1, 1e-6);
  EXPECT_NEAR(Project(projection, 2, 1, -1)[1], 1, 1e-6);
  EXPECT_NEAR(Project(projection, 2, 1, -2)[0], 0.5f, 1e-6);
  projection = Jade::Detail::ProjectionMatrix(camera, 1);
  EXPECT_NEAR(Project(projection, 2, 1, -1)[0], 2, 1e-6);

  camera.setOrthographic(4, 1, 11);
  projection = Jade::Detail::ProjectionMatrix(camera, 2);
  EXPECT_NEAR(Project(projection, 0, 0, -1)[2], -1, 1e-6);
  EXPECT_NEAR(Project(projection, 0, 0, -11)[2], 1, 1e-6);
  EXPECT_NEAR(Project(projection, 4, 2, -1)[0], 1, 1e-6);
  EXPECT_NEAR(Project(projection, 4, 2, -1)[1], 1, 1e-6);
  EXPECT_FLOAT_EQ(Project(projection, 2, 1, -2)[0], Project(projection, 2, 1, -10)[0]);
  EXPECT_THROW(Jade::Detail::ProjectionMatrix(camera, 0), std::invalid_argument);
}

TEST(Matrix, ModelAppliesScaleThenRotationThenTranslation)
{
  Jade::Object cube;
  cube.setMesh(Jade::Mesh::cube(2));
  cube.setPos({3, 4, 5});
  cube.setRotation({0, 0, 90});
  cube.setScale({2, 1, 1});
  const auto result = Transform(Jade::Detail::ModelMatrix(cube), 1, 0, 0);
  EXPECT_NEAR(result[0], 3, 1e-6);
  EXPECT_NEAR(result[1], 6, 1e-6);
  EXPECT_NEAR(result[2], 5, 1e-6);
  EXPECT_FLOAT_EQ(result[3], 1);
}

TEST(Matrix, NormalsStayPerpendicularUnderNonuniformScaleAndRotation)
{
  Jade::Object object;
  object.setPos({10, 20, 30});
  object.setRotation({25, 40, 70});
  object.setScale({2, 3, 0.5f});
  const auto model = Jade::Detail::ModelMatrix(object);
  const auto origin = Transform(model, 0, 0, 0);
  const auto tangent = Transform(model, 1, -1, 0);
  const auto normal = Transform(Jade::Detail::NormalMatrix(object), 1, 1, 0);
  const Jade::Vec3 worldTangent{tangent[0] - origin[0], tangent[1] - origin[1], tangent[2] - origin[2]};
  EXPECT_NEAR(Jade::dot(worldTangent, {normal[0], normal[1], normal[2]}), 0, 1e-5f);
  // Translation must not affect the normal transform.
  object.setPos({-30, -20, -10});
  EXPECT_EQ(Transform(Jade::Detail::NormalMatrix(object), 1, 1, 0), normal);
}
