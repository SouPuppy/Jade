#include <Jade/Jade.hpp>
#include "internal/Platform/Window.hpp"
#include "internal/Renderer/Renderer.hpp"
#include "internal/Renderer/RenderSystem.hpp"

#include <gtest/gtest.h>
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <vector>

namespace {
Jade::WindowConfig HiddenWindow()
{
  Jade::WindowConfig config;
  config.visible = false; config.width = 320; config.height = 240;
  return config;
}
std::array<unsigned char, 4> Pixel(int x, int y)
{
  std::array<unsigned char, 4> result{};
  glReadBuffer(GL_BACK);
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
  return result;
}
struct Bounds {
  int left;
  int right = -1;
  int bottom;
  int top = -1;
  int coloredPixels = 0;

  int Width() const { return right >= left ? right - left + 1 : 0; }
  int Height() const { return top >= bottom ? top - bottom + 1 : 0; }
};

Bounds ColoredBounds(Jade::FramebufferSize size)
{
  std::vector<unsigned char> pixels(static_cast<std::size_t>(size.width) * size.height * 4);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, size.width, size.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  Bounds result{size.width, -1, size.height, -1};
  const Jade::Color background;
  const std::array<int, 3> expected{
    static_cast<int>(std::lround(background.red * 255)),
    static_cast<int>(std::lround(background.green * 255)),
    static_cast<int>(std::lround(background.blue * 255))
  };
  for (int y = 0; y < size.height; ++y) {
    for (int x = 0; x < size.width; ++x) {
      const auto offset = (static_cast<std::size_t>(y) * size.width + x) * 4;
      if (std::abs(pixels[offset] - expected[0]) > 3
          || std::abs(pixels[offset + 1] - expected[1]) > 3
          || std::abs(pixels[offset + 2] - expected[2]) > 3) {
        ++result.coloredPixels;
        result.left = std::min(result.left, x);
        result.right = std::max(result.right, x);
        result.bottom = std::min(result.bottom, y);
        result.top = std::max(result.top, y);
      }
    }
  }
  return result;
}

struct RenderFixture {
  Jade::Window window{HiddenWindow()};
  Jade::Renderer renderer{window, false};
  Jade::Assets assets;
  Jade::Scene contents;
  Jade::Entity camera = contents.create();
  RenderFixture()
  {
    camera.add<Jade::Transform>().position = {0, 0, 5};
    camera.add<Jade::Camera>();
    contents.setCamera(camera);
  }
  Jade::Transform& Pose() { return camera.get<Jade::Transform>(); }
  Jade::Camera& Lens() { return camera.get<Jade::Camera>(); }
  Jade::FramebufferSize Size() { return window.GetFramebufferSize(); }
  Jade::Entity Create(Jade::MeshHandle mesh, Jade::Vec3 position = {}, Jade::Color color = {1, 1, 1, 1})
  {
    const auto entity = contents.create();
    entity.add<Jade::Transform>().position = position;
    entity.add<Jade::MeshRenderer>(mesh, color);
    return entity;
  }
  void Render(bool axes = false)
  {
    Jade::Detail::RenderSystem::Render(contents, assets, renderer, Size(), axes);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
  }
  Bounds Draw(bool axes = false)
  {
    EXPECT_TRUE(renderer.BeginFrame(Size(), {}));
    Render(axes);
    return ColoredBounds(Size());
  }
  void Resize(Jade::FramebufferSize size)
  {
    glfwSetWindowSize(glfwGetCurrentContext(), size.width, size.height);
    window.PollEvents();
  }
  std::array<int, 2> Project(Jade::Vec3 point)
  {
    const auto relative = point - Pose().position;
    double x = Jade::dot(relative, Jade::Motion::getRight(Pose()));
    double y = Jade::dot(relative, Jade::Motion::getUp(Pose()));
    const auto size = Size();
    const double aspect = static_cast<double>(size.width) / size.height;
    const double halfHeight = Lens().projection == Jade::Projection::Perspective
      ? Jade::dot(relative, Jade::Motion::getFront(Pose()))
        * std::tan(Lens().fovDegrees * 3.14159265358979323846 / 360)
      : Lens().viewHeight / 2;
    return {static_cast<int>((x / (halfHeight * aspect) + 1) * size.width / 2),
      static_cast<int>((y / halfHeight + 1) * size.height / 2)};
  }
  std::array<unsigned char, 4> At(Jade::Vec3 point)
  {
    const auto xy = Project(point); return Pixel(xy[0], xy[1]);
  }
  bool HasColor(Jade::Vec3 point, std::array<int, 3> color)
  {
    const auto xy = Project(point), size = std::array<int, 2>{Size().width, Size().height};
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
      const int x = xy[0] + dx, y = xy[1] + dy;
      if (x < 0 || y < 0 || x >= size[0] || y >= size[1]) continue;
      const auto pixel = Pixel(x, y);
      if (std::abs(pixel[0] - color[0]) < 10 && std::abs(pixel[1] - color[1]) < 10
          && std::abs(pixel[2] - color[2]) < 10) return true;
    }
    return false;
  }
};
} // namespace

TEST(Renderer, IndexedAssetsAndHandleReplacementChangeTheNextDraw)
{
  RenderFixture scene;
  scene.Lens().projection = Jade::Projection::Orthographic; scene.Lens().viewHeight = 4;
  Jade::Mesh mesh;
  mesh.vertices = {
    {{-1.8f, -0.8f, 0}, {0, 0, 1}}, {{-0.2f, -0.8f, 0}, {0, 0, 1}}, {{-1, 0.8f, 0}, {0, 0, 1}},
    {{0.2f, -0.8f, 0}, {0, 0, 1}}, {{1.8f, -0.8f, 0}, {0, 0, 1}}, {{1, 0.8f, 0}, {0, 0, 1}}
  };
  mesh.indices = {3, 4, 5};
  const auto entity = scene.Create(scene.assets.add(mesh), {}, {1, 0, 0, 1});
  ASSERT_GT(scene.Draw().coloredPixels, 1000);
  EXPECT_NEAR(scene.At({-1, 0, 0})[0], Jade::Color{}.red * 255, 2);
  EXPECT_GT(scene.At({1, 0, 0})[0], 200);
  mesh.indices = {0, 1, 2};
  entity.get<Jade::MeshRenderer>().mesh = scene.assets.add(mesh);
  scene.Draw();
  EXPECT_GT(scene.At({-1, 0, 0})[0], 200);
  EXPECT_NEAR(scene.At({1, 0, 0})[0], Jade::Color{}.red * 255, 2);
  entity.get<Jade::MeshRenderer>().mesh = {};
  EXPECT_EQ(scene.Draw().coloredPixels, 0);
}

TEST(Renderer, SphereHasRoundSilhouetteSmoothLightingAndWireframe)
{
  RenderFixture scene;
  scene.Lens().projection = Jade::Projection::Orthographic;
  scene.Lens().viewHeight = 4;
  scene.Create(scene.assets.add(Jade::Mesh::sphere(1)), {}, {1, 0, 0, 1});
  const auto filled = scene.Draw();
  ASSERT_GT(filled.coloredPixels, 10000);
  EXPECT_NEAR(filled.Width(), 120, 2);
  EXPECT_NEAR(filled.Height(), 120, 2);
  // A disk covers about pi/4 of its bounding rectangle; square corners stay clear.
  const double coverage = static_cast<double>(filled.coloredPixels) / (filled.Width() * filled.Height());
  EXPECT_GT(coverage, 0.75);
  EXPECT_LT(coverage, 0.81);
  EXPECT_NEAR(scene.At({0.8f, 0.8f, 0})[0], Jade::Color{}.red * 255, 2);
  EXPECT_GT(scene.At({})[0], 180);
  EXPECT_LT(scene.At({})[2], 5);
  EXPECT_GT(scene.At({0, 0.5f, 0})[0], scene.At({0, -0.5f, 0})[0] + 60);
  scene.renderer.setWireframe(true);
  const auto wire = scene.Draw();
  EXPECT_GT(wire.coloredPixels, 1000);
  EXPECT_LT(wire.coloredPixels, filled.coloredPixels);
}

TEST(Renderer, SharedMeshesRenderWithIndependentPoseAndColor)
{
  RenderFixture scene;
  scene.Lens().projection = Jade::Projection::Orthographic;
  const auto mesh = scene.assets.add(Jade::Mesh::cube());
  const auto red = scene.Create(mesh, {-1, 0, 0}, {1, 0, 0, 1});
  const auto blue = scene.Create(mesh, {1, 0, 0}, {0, 0, 1, 1});
  scene.Draw();
  EXPECT_GT(scene.At({-1, 0, 0})[0], 200);
  EXPECT_LT(scene.At({-1, 0, 0})[2], 10);
  EXPECT_GT(scene.At({1, 0, 0})[2], 200);
  red.get<Jade::Transform>().position.y = 1;
  red.get<Jade::MeshRenderer>().color = {0, 1, 0, 1};
  scene.Draw();
  EXPECT_GT(scene.At({-1, 1, 0})[1], 200);
  EXPECT_GT(scene.At({1, 0, 0})[2], 200);
  blue.remove<Jade::Transform>();
  scene.Draw();
  EXPECT_NEAR(scene.At({1, 0, 0})[2], Jade::Color{}.blue * 255, 2);
}

TEST(Renderer, NormalsControlLightingAndRotateWithTransform)
{
  RenderFixture scene;
  scene.Lens().projection = Jade::Projection::Orthographic; scene.Lens().viewHeight = 4;
  auto mesh = Jade::Mesh::triangle({-1, -1, 0}, {1, -1, 0}, {0, 1, 0});
  for (auto& vertex : mesh.vertices) vertex.normal = {1, 0, 0};
  const auto entity = scene.Create(scene.assets.add(mesh));
  scene.Draw(); const auto initial = scene.At({})[0];
  entity.get<Jade::Transform>().rotation = Jade::Quaternion::angleAxis({0, 0, 1}, 90);
  scene.Draw(); const auto rotated = scene.At({})[0];
  EXPECT_GT(rotated, initial + 40);
  entity.get<Jade::Transform>().rotation = {};
  for (auto& vertex : mesh.vertices) vertex.normal = {0, 0, 1};
  entity.get<Jade::MeshRenderer>().mesh = scene.assets.add(mesh);
  scene.Draw(); EXPECT_GT(scene.At({})[0], rotated + 30);
}

TEST(Renderer, BunnyAndCubeRenderInBothProjectionsAndWireframeRestoresFill)
{
  RenderFixture scene;
  for (bool bunny : {false, true}) {
    const auto mesh = bunny ? scene.assets.load<Jade::Mesh>(std::filesystem::path(JADE_TEST_ASSET_DIR) / "bunny.obj")
      : scene.assets.add(Jade::Mesh::cube(2));
    const auto entity = scene.Create(mesh);
    if (bunny) {
      entity.get<Jade::Transform>().scale = {16, 16, 16};
      entity.get<Jade::Transform>().position = {0.28f, -1.72f, 0};
    }
    for (auto mode : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
      scene.Lens().projection = mode;
      scene.renderer.setWireframe(false); const auto filled = scene.Draw();
      EXPECT_GT(filled.coloredPixels, 1000);
      EXPECT_GT(filled.Width(), 50); EXPECT_GT(filled.Height(), 50);
      scene.renderer.setWireframe(true); const auto wire = scene.Draw();
      EXPECT_GT(wire.coloredPixels, 100);
      if (!bunny) EXPECT_LT(wire.coloredPixels, filled.coloredPixels / 4);
      EXPECT_NEAR(wire.Width(), filled.Width(), 2); EXPECT_NEAR(wire.Height(), filled.Height(), 2);
      scene.renderer.setWireframe(false);
      EXPECT_EQ(scene.Draw().coloredPixels, filled.coloredPixels);
    }
    scene.contents.destroy(entity);
  }
}

TEST(Renderer, ResizePreservesCubeAspectAndCenteredProjection)
{
  RenderFixture scene; scene.Create(scene.assets.add(Jade::Mesh::cube(2)));
  for (auto mode : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
    scene.Lens().projection = mode; scene.Lens().fovDegrees = 90;
    int previousWidth = 0, previousHeight = 0;
    for (auto requested : {Jade::FramebufferSize{320, 240}, {480, 240}, {240, 320}}) {
      scene.Resize(requested); const auto size = scene.Size(); const auto bounds = scene.Draw();
      ASSERT_GT(bounds.Width(), 10);
      EXPECT_NEAR(bounds.Width(), bounds.Height(), 2);
      EXPECT_NEAR((bounds.left + bounds.right) / 2.0, (size.width - 1) / 2.0, 2);
      EXPECT_NEAR((bounds.bottom + bounds.top) / 2.0, (size.height - 1) / 2.0, 2);
      if (previousWidth > 0) EXPECT_NEAR(bounds.Width(), static_cast<double>(previousWidth) * size.height / previousHeight, 2);
      previousWidth = bounds.Width(); previousHeight = size.height;
      scene.renderer.Present();
    }
  }
}

TEST(Renderer, PerspectiveDistanceAndFovChangeSizeOrthographicDistanceDoesNot)
{
  RenderFixture scene; scene.Create(scene.assets.add(Jade::Mesh::cube(2)));
  scene.Lens().fovDegrees = 90; scene.Pose().position.z = 4;
  const auto nearBounds = scene.Draw(); scene.Pose().position.z = 8;
  const auto farBounds = scene.Draw(); ASSERT_GT(farBounds.Width(), 0);
  EXPECT_GT(nearBounds.Width(), farBounds.Width() * 1.8);
  scene.Pose().position.z = 5; scene.Lens().fovDegrees = 30;
  const auto narrow = scene.Draw(); scene.Lens().fovDegrees = 90;
  EXPECT_GT(narrow.Width(), scene.Draw().Width() * 3.0);
  scene.Lens().projection = Jade::Projection::Orthographic;
  scene.Pose().position.z = 4; const auto orthoNear = scene.Draw();
  scene.Pose().position.z = 8; EXPECT_NEAR(orthoNear.Width(), scene.Draw().Width(), 1);
}

TEST(Renderer, DepthIsIndependentOfCreationOrderAndClipPlanesRejectMeshes)
{
  RenderFixture scene;
  const auto cube = scene.assets.add(Jade::Mesh::cube(2));
  for (bool nearFirst : {true, false}) {
    const auto first = scene.Create(cube, nearFirst ? Jade::Vec3{} : Jade::Vec3{0, 0, -2},
      nearFirst ? Jade::Color{1, 0, 0, 1} : Jade::Color{0, 0, 1, 1});
    const auto second = scene.Create(cube, nearFirst ? Jade::Vec3{0, 0, -2} : Jade::Vec3{},
      nearFirst ? Jade::Color{0, 0, 1, 1} : Jade::Color{1, 0, 0, 1});
    for (auto mode : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
      scene.Lens().projection = mode; scene.Draw();
      EXPECT_GT(scene.At({})[0], 200); EXPECT_LT(scene.At({})[2], 10);
      float depth = 1; glReadPixels(scene.Size().width / 2, scene.Size().height / 2, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
      EXPECT_GT(depth, 0); EXPECT_LT(depth, 1);
      scene.Lens().farPlane = 3; EXPECT_EQ(scene.Draw().Width(), 0);
      scene.Lens().farPlane = 20; scene.Lens().nearPlane = 9;
      EXPECT_EQ(scene.Draw().Width(), 0);
      scene.Lens().nearPlane = 0.1f; scene.Lens().farPlane = 100;
    }
    scene.contents.destroy(first); scene.contents.destroy(second);
  }
}

TEST(Renderer, WorldLinesAndTrianglesShareProjectionAndDepth)
{
  RenderFixture scene;
  const auto triangle = scene.assets.add(Jade::Mesh::triangle({-1, -1, 0}, {1, -1, 0}, {0, 1, 0}));
  const auto line = scene.assets.add(Jade::Mesh::line({-0.5f, 0, 0}, {0.5f, 0, 0}));
  for (bool lineFirst : {true, false}) {
    const auto first = lineFirst ? scene.Create(line, {0, 0, -1}, {0, 1, 0, 1})
      : scene.Create(triangle, {}, {1, 0, 0, 1});
    const auto second = lineFirst ? scene.Create(triangle, {}, {1, 0, 0, 1})
      : scene.Create(line, {0, 0, -1}, {0, 1, 0, 1});
    const auto lineEntity = lineFirst ? first : second;
    for (auto mode : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
      scene.Lens().projection = mode;
      lineEntity.get<Jade::Transform>().position.z = -1;
      scene.Draw(); EXPECT_FALSE(scene.HasColor({}, {0, 255, 0})); EXPECT_GT(scene.At({})[0], 200);
      lineEntity.get<Jade::Transform>().position.z = 1;
      scene.Draw(); EXPECT_TRUE(scene.HasColor({0, 0, 1}, {0, 255, 0}));
    }
    scene.contents.destroy(first); scene.contents.destroy(second);
  }
}

TEST(Renderer, TriangleWireframeAndLinesRemainVisibleAfterResize)
{
  RenderFixture scene;
  scene.Create(scene.assets.add(Jade::Mesh::triangle({-1, -1, 0}, {1, -1, 0}, {0, 1, 0})), {}, {1, 0, 0, 1});
  const auto line = scene.Create(scene.assets.add(Jade::Mesh::line({-1, 1.5f, 0}, {1, 1.5f, 0})));
  for (auto mode : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
    scene.Lens().projection = mode;
    for (auto size : {Jade::FramebufferSize{320, 240}, {480, 240}, {240, 320}}) {
      SCOPED_TRACE(static_cast<int>(mode));
        SCOPED_TRACE(size.width);
      scene.Resize(size); scene.renderer.setWireframe(false);
      const auto filled = scene.Draw(); EXPECT_GT(scene.At({})[0], 200);
      EXPECT_TRUE(scene.HasColor({0, 1.5f, 0}, {255, 255, 255}));
      scene.renderer.setWireframe(true); const auto wire = scene.Draw();
      EXPECT_GT(wire.coloredPixels, 100); EXPECT_LT(wire.coloredPixels, filled.coloredPixels / 3);
      EXPECT_NEAR(scene.At({})[0], Jade::Color{}.red * 255, 2);
      EXPECT_TRUE(scene.HasColor({0, 1.5f, 0}, {255, 255, 255}));
      scene.renderer.Present();
    }
  }
  line.get<Jade::Transform>().position.y = -3;
  scene.Draw();
  EXPECT_TRUE(scene.HasColor({0, -1.5f, 0}, {255, 255, 255}));
  EXPECT_FALSE(scene.HasColor({0, 1.5f, 0}, {255, 255, 255}));
}

TEST(Renderer, AxisGizmoHasUnitWorldAxesWithCameraRotationAndResize)
{
  RenderFixture scene; scene.Pose().position = {3, 3, 5};
  Jade::Motion::lookAt(scene.Pose(), {}); const auto base = scene.Pose().rotation;
  constexpr std::array<Jade::Vec3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  constexpr std::array<std::array<int, 3>, 3> colors{{{255, 0, 0}, {0, 255, 0}, {0, 0, 255}}};
  for (auto mode : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
    scene.Lens().projection = mode;
    for (double roll : {0.0, 12.0}) {
      scene.Pose().rotation = base; Jade::Motion::roll(scene.Pose(), roll);
      for (auto size : {Jade::FramebufferSize{320, 240}, {240, 320}}) {
        SCOPED_TRACE(static_cast<int>(mode));
        SCOPED_TRACE(roll);
        SCOPED_TRACE(size.width);
        scene.Resize(size); scene.Draw(true);
        for (std::size_t axis = 0; axis < axes.size(); ++axis) {
          EXPECT_TRUE(scene.HasColor(axes[axis] * 0.5f, colors[axis]));
          EXPECT_TRUE(scene.HasColor(axes[axis] * 0.9f, colors[axis]));
          EXPECT_FALSE(scene.HasColor(axes[axis] * 1.35f, colors[axis]));
          EXPECT_FALSE(scene.HasColor(axes[axis] * -0.35f, colors[axis]));
        }
        scene.renderer.Present();
      }
    }
  }
}

TEST(Renderer, GizmoDrawsOverMeshesAndRestoresDepthAndPolygonState)
{
  RenderFixture scene; scene.Pose().position = {3, 3, 5}; Jade::Motion::lookAt(scene.Pose(), {});
  const auto cube = scene.Create(scene.assets.add(Jade::Mesh::cube(2)));
  for (bool wire : {false, true}) {
    scene.renderer.setWireframe(wire); scene.Draw(true);
    EXPECT_TRUE(scene.HasColor({0.5f, 0, 0}, {255, 0, 0}));
    EXPECT_TRUE(scene.HasColor({0, 0.5f, 0}, {0, 255, 0}));
    EXPECT_TRUE(scene.HasColor({0, 0, 0.5f}, {0, 0, 255}));
    EXPECT_EQ(glIsEnabled(GL_DEPTH_TEST), GL_TRUE);
    GLboolean write = GL_FALSE; glGetBooleanv(GL_DEPTH_WRITEMASK, &write); EXPECT_EQ(write, GL_TRUE);
    std::array<GLint, 2> mode{}; glGetIntegerv(GL_POLYGON_MODE, mode.data());
    EXPECT_EQ(mode[0], wire ? GL_LINE : GL_FILL);
  }
  scene.contents.destroy(cube);
  glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); scene.Render(true);
  EXPECT_EQ(glIsEnabled(GL_DEPTH_TEST), GL_FALSE);
  GLboolean write = GL_TRUE; glGetBooleanv(GL_DEPTH_WRITEMASK, &write); EXPECT_EQ(write, GL_FALSE);
}

TEST(Renderer, MissingActiveCameraClearsAndCanResumeWithAValidCamera)
{
  RenderFixture scene;
  scene.Create(scene.assets.add(Jade::Mesh::cube()));
  const auto previous = scene.camera;
  scene.camera.remove<Jade::Camera>(); EXPECT_EQ(scene.Draw(true).coloredPixels, 0);
  scene.camera.add<Jade::Camera>(); EXPECT_GT(scene.Draw().coloredPixels, 100);
  scene.camera.remove<Jade::Transform>(); EXPECT_EQ(scene.Draw(true).coloredPixels, 0);
  scene.contents.destroy(scene.camera); EXPECT_EQ(scene.Draw(true).coloredPixels, 0);
  scene.camera = scene.contents.create();
  scene.camera.add<Jade::Transform>().position = {0, 0, 5}; scene.camera.add<Jade::Camera>();
  scene.contents.setCamera(scene.camera);
  EXPECT_NE(scene.camera.id(), previous.id()); EXPECT_GT(scene.Draw().coloredPixels, 100);
  EXPECT_FALSE(scene.renderer.BeginFrame({0, 0}, {}));
  Jade::Detail::RenderSystem::Render(scene.contents, scene.assets, scene.renderer, {0, 0}, true);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(Renderer, ParentTransformsMoveChildrenAndComponentRemovalUpdatesPixels)
{
  RenderFixture scene;
  const auto child = scene.Create(scene.assets.add(Jade::Mesh::cube(0.5f)));
  const auto parent = scene.contents.create();
  auto& pose = parent.add<Jade::Transform>();
  pose.position = {-1, 0, 0};
  scene.contents.setParent(child, parent);
  const auto left = scene.Draw();
  pose.position = {1, 0, 0};
  const auto right = scene.Draw();
  EXPECT_GT(right.left, left.right);
  parent.remove<Jade::Transform>();
  const auto centered = scene.Draw();
  EXPECT_NEAR((centered.left + centered.right) / 2.0, scene.Size().width / 2.0, 2);
  EXPECT_EQ(scene.contents.parent(child), parent);
  scene.contents.destroy(parent);
  EXPECT_EQ(scene.Draw().coloredPixels, 0);
}

TEST(Renderer, HierarchicalNormalsHandleNonUniformScaleAndChildRotation)
{
  RenderFixture scene;
  const auto mesh = scene.assets.add(Jade::Mesh::triangle({-1, -1, 0}, {1, -1, 0}, {0, 1, 0}));
  const auto child = scene.Create(mesh);
  child.get<Jade::Transform>().rotation = Jade::Quaternion::angleAxis({0, 1, 0}, 45);
  const auto parent = scene.contents.create();
  parent.add<Jade::Transform>().scale = {2, 0.5f, 1};
  scene.contents.setParent(child, parent);
  scene.Draw();
  const auto pixel = Pixel(scene.Size().width / 2, scene.Size().height / 2);
  // inverse-transpose gives unit normal (1/sqrt(5), 0, 2/sqrt(5)).
  const double diffuse = (0.3 + 2.0) / std::sqrt(5.0 * (0.3 * 0.3 + 0.7 * 0.7 + 1));
  const double expected = 255 * (0.25 + 0.75 * diffuse);
  EXPECT_NEAR(pixel[0], expected, 3);
  EXPECT_NEAR(pixel[1], expected, 3);
  EXPECT_NEAR(pixel[2], expected, 3);
}

TEST(Renderer, ParentedCameraInheritsEyePositionWithoutScalingProjection)
{
  RenderFixture scene;
  scene.Create(scene.assets.add(Jade::Mesh::cube()), {2, 0, 0});
  const auto parent = scene.contents.create();
  auto& pose = parent.add<Jade::Transform>();
  pose.position = {2, 0, 0}; pose.scale = {3, 2, 1};
  scene.contents.setParent(scene.camera, parent);
  scene.Pose().scale = {10, 20, 30};
  const auto inherited = scene.Draw();
  EXPECT_NEAR((inherited.left + inherited.right) / 2.0, scene.Size().width / 2.0, 2);
  scene.contents.setParent(scene.camera);
  scene.Pose().position = {2, 0, 5};
  const auto root = scene.Draw();
  EXPECT_EQ(root.left, inherited.left); EXPECT_EQ(root.right, inherited.right);
  EXPECT_EQ(root.top, inherited.top); EXPECT_EQ(root.bottom, inherited.bottom);
}

TEST(Renderer, EmptyAndDegenerateMeshesDoNotDrawAndInvalidDataIsRejected)
{
  RenderFixture scene;
  scene.Create(scene.assets.add(Jade::Mesh{}));
  scene.Create(scene.assets.add(Jade::Mesh::line({}, {})));
  scene.Create(scene.assets.add(Jade::Mesh::triangle({}, {1, 1, 1}, {2, 2, 2})));
  EXPECT_EQ(scene.Draw().coloredPixels, 0);
  const auto entity = scene.Create(scene.assets.add(Jade::Mesh::cube()));
  entity.get<Jade::MeshRenderer>().color.red = 2;
  EXPECT_THROW(scene.Draw(), std::invalid_argument);
  entity.get<Jade::MeshRenderer>().color = {1, 1, 1, 1};
  Jade::Assets other; entity.get<Jade::MeshRenderer>().mesh = other.add(Jade::Mesh::cube());
  EXPECT_THROW(scene.Draw(), std::invalid_argument);
}
