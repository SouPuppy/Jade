#include <Jade/Objects/Camera.hpp>
#include <Jade/Objects/Object.hpp>
#include <Jade/Geometry/Line.hpp>
#include <Jade/Geometry/Triangle.hpp>

#include "internal/Platform/Window.hpp"
#include "internal/Renderer/Renderer.hpp"

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
  config.visible = false;
  config.width = 320;
  config.height = 240;
  return config;
}

std::array<unsigned char, 4> Pixel(int x, int y)
{
  std::array<unsigned char, 4> result{};
  glReadBuffer(GL_BACK);
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
  return result;
}

std::array<unsigned char, 4> OverlayPixel(Jade::Vec2 point, Jade::FramebufferSize size)
{
  const float scale = static_cast<float>(size.height) / 10.0f;
  return Pixel(static_cast<int>(point.x * scale), static_cast<int>(point.y * scale));
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

Bounds DrawObject(Jade::Renderer& renderer, Jade::Window& window, const Jade::Object& cube, const Jade::Camera& camera)
{
  const auto size = window.GetFramebufferSize();
  EXPECT_TRUE(renderer.BeginFrame(size, {}));
  renderer.DrawObject(cube, camera);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
  return ColoredBounds(size);
}

std::array<int, 2> WorldPixel(Jade::Vec3 point, const Jade::Camera& camera, Jade::FramebufferSize size)
{
  const auto relative = point - camera.getPos();
  double x = Jade::dot(relative, camera.getRight());
  double y = Jade::dot(relative, camera.getUp());
  const double aspect = static_cast<double>(size.width) / size.height;
  if (camera.Mode() == Jade::Projection::Perspective) {
    const double halfHeight = Jade::dot(relative, camera.getFront())
      * std::tan(camera.FOV() * 3.14159265358979323846 / 360.0);
    x /= halfHeight * aspect;
    y /= halfHeight;
  } else {
    x /= camera.ViewHeight() * aspect / 2.0;
    y /= camera.ViewHeight() / 2.0;
  }
  return {static_cast<int>((x + 1.0) * size.width / 2.0),
    static_cast<int>((y + 1.0) * size.height / 2.0)};
}

bool HasAxisColorNear(Jade::Vec3 point, const Jade::Camera& camera,
  Jade::FramebufferSize size, const std::array<int, 3>& color)
{
  const auto center = WorldPixel(point, camera, size);
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const int x = center[0] + dx, y = center[1] + dy;
      if (x < 0 || y < 0 || x >= size.width || y >= size.height) {
        continue;
      }
      const auto pixel = Pixel(x, y);
      if (std::abs(pixel[0] - color[0]) < 10 && std::abs(pixel[1] - color[1]) < 10
          && std::abs(pixel[2] - color[2]) < 10) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

TEST(Renderer, IndexedMeshUsesFaceIndicesAndReplacementChangesTheNextDraw)
{
  Jade::Window window(HiddenWindow());
  Jade::Renderer renderer(window, false);
  Jade::Camera camera;
  camera.setPos({0, 0, 5});
  camera.lookAt({0, 0, 0});
  camera.setOrthographic(4);
  Jade::Mesh mesh;
  mesh.vertices = {
    {{-1.8f, -0.8f, 0}, {0, 0, 1}}, {{-0.2f, -0.8f, 0}, {0, 0, 1}}, {{-1, 0.8f, 0}, {0, 0, 1}},
    {{0.2f, -0.8f, 0}, {0, 0, 1}}, {{1.8f, -0.8f, 0}, {0, 0, 1}}, {{1, 0.8f, 0}, {0, 0, 1}}
  };
  mesh.indices = {3, 4, 5};
  Jade::Object object;
  object.setMesh(mesh);
  object.setColor({1, 0, 0, 1});
  const auto size = window.GetFramebufferSize();
  ASSERT_GT(DrawObject(renderer, window, object, camera).coloredPixels, 1000);
  auto left = WorldPixel({-1, 0, 0}, camera, size);
  auto right = WorldPixel({1, 0, 0}, camera, size);
  EXPECT_NEAR(Pixel(left[0], left[1])[0], Jade::Color{}.red * 255, 2);
  EXPECT_GT(Pixel(right[0], right[1])[0], 200);
  mesh.indices = {0, 1, 2};
  object.setMesh(std::move(mesh));
  DrawObject(renderer, window, object, camera);
  EXPECT_GT(Pixel(left[0], left[1])[0], 200);
  EXPECT_NEAR(Pixel(right[0], right[1])[0], Jade::Color{}.red * 255, 2);
  object.setMesh({});
  EXPECT_EQ(DrawObject(renderer, window, object, camera).coloredPixels, 0);
}

TEST(Renderer, MeshNormalsControlLightingAndRotateWithTheObject)
{
  Jade::Window window(HiddenWindow());
  Jade::Renderer renderer(window, false);
  Jade::Camera camera;
  camera.setPos({0, 0, 5});
  camera.lookAt({0, 0, 0});
  camera.setOrthographic(4);
  Jade::Mesh mesh;
  mesh.vertices = {{{-1, -1, 0}, {1, 0, 0}}, {{1, -1, 0}, {1, 0, 0}}, {{0, 1, 0}, {1, 0, 0}}};
  mesh.indices = {0, 1, 2};
  Jade::Object object;
  object.setMesh(mesh);
  const auto size = window.GetFramebufferSize();
  DrawObject(renderer, window, object, camera);
  const auto initial = Pixel(size.width / 2, size.height / 2)[0];
  object.setRotation({0, 0, 90}); // Normal changes from +X to +Y; same center remains inside.
  DrawObject(renderer, window, object, camera);
  const auto rotated = Pixel(size.width / 2, size.height / 2)[0];
  EXPECT_GT(rotated, initial + 40);
  object.setRotation({0, 0, 0});
  for (auto& vertex : mesh.vertices) {
    vertex.normal = {0, 0, 1};
  }
  object.setMesh(mesh);
  DrawObject(renderer, window, object, camera);
  EXPECT_GT(Pixel(size.width / 2, size.height / 2)[0], rotated + 30);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(Renderer, LoadedBunnyRendersWithTransformsInBothProjectionModesAndWireframe)
{
  Jade::Window window(HiddenWindow());
  Jade::Renderer renderer(window, false);
  Jade::Camera camera;
  camera.setPos({0, 0, 5});
  camera.lookAt({0, 0, 0});
  Jade::Object bunny;
  bunny.setMesh(Jade::Mesh::load(std::filesystem::path(JADE_TEST_ASSET_DIR) / "bunny.obj"));
  bunny.setScale({16, 16, 16});
  bunny.setPos({0.28f, -1.72f, 0});
  for (auto mode : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
    camera.setProjection(mode);
    renderer.setWireframe(false);
    const auto filled = DrawObject(renderer, window, bunny, camera);
    EXPECT_GT(filled.coloredPixels, 1000);
    EXPECT_GT(filled.Width(), 50);
    EXPECT_GT(filled.Height(), 50);
    renderer.setWireframe(true);
    const auto wire = DrawObject(renderer, window, bunny, camera);
    EXPECT_GT(wire.coloredPixels, 100);
    EXPECT_NEAR(wire.Width(), filled.Width(), 2);
    EXPECT_NEAR(wire.Height(), filled.Height(), 2);
    renderer.setWireframe(false);
    EXPECT_EQ(DrawObject(renderer, window, bunny, camera).coloredPixels, filled.coloredPixels);
  }
}

TEST(Renderer, AxisGizmoHasUnitWorldAxesInBothProjectionsAndFollowsCamera)
{
  Jade::Window window(HiddenWindow());
  Jade::Renderer renderer(window, false);
  Jade::Camera camera;
  constexpr std::array<Jade::Vec3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  constexpr std::array<std::array<int, 3>, 3> colors{{{255, 0, 0}, {0, 255, 0}, {0, 0, 255}}};
  for (const auto projection : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
    camera.setProjection(projection);
    for (const Jade::Vec3 rotation : {Jade::Vec3{}, Jade::Vec3{5, 8, 12}}) {
      SCOPED_TRACE(static_cast<int>(projection));
      SCOPED_TRACE(rotation.z);
      camera.setRotation(rotation);
      for (const Jade::FramebufferSize requested : {Jade::FramebufferSize{320, 240},
          Jade::FramebufferSize{240, 320}}) {
        glfwSetWindowSize(glfwGetCurrentContext(), requested.width, requested.height);
        window.PollEvents();
        const auto size = window.GetFramebufferSize();
        ASSERT_TRUE(renderer.BeginFrame(size, {}));
        SCOPED_TRACE(size.width);
        renderer.DrawAxisGizmo(camera);
        for (std::size_t axis = 0; axis < axes.size(); ++axis) {
          EXPECT_TRUE(HasAxisColorNear(axes[axis] * 0.5f, camera, size, colors[axis]));
          EXPECT_TRUE(HasAxisColorNear(axes[axis] * 0.9f, camera, size, colors[axis]));
          EXPECT_FALSE(HasAxisColorNear(axes[axis] * 1.35f, camera, size, colors[axis]));
          EXPECT_FALSE(HasAxisColorNear(axes[axis] * -0.35f, camera, size, colors[axis]));
        }
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
        renderer.Present();
      }
    }
  }
  EXPECT_FALSE(renderer.BeginFrame({0, 0}, {}));
  renderer.DrawAxisGizmo(camera);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(Renderer, AxisGizmoIsVisibleOverCubeAndPreservesDepthAndPolygonState)
{
  Jade::Window window(HiddenWindow());
  Jade::Renderer renderer(window, false);
  const Jade::Camera camera;
  Jade::Object cube;
  cube.setMesh(Jade::Mesh::cube(2));
  const auto size = window.GetFramebufferSize();
  constexpr std::array<Jade::Vec3, 3> midpoints{{{0.5f, 0, 0}, {0, 0.5f, 0}, {0, 0, 0.5f}}};
  constexpr std::array<std::array<int, 3>, 3> colors{{{255, 0, 0}, {0, 255, 0}, {0, 0, 255}}};
  for (const bool wireframe : {false, true}) {
    renderer.setWireframe(wireframe);
    ASSERT_TRUE(renderer.BeginFrame(size, {}));
    renderer.DrawObject(cube, camera);
    for (std::size_t axis = 0; axis < midpoints.size(); ++axis) {
      EXPECT_FALSE(HasAxisColorNear(midpoints[axis], camera, size, colors[axis]));
    }
    renderer.DrawAxisGizmo(camera);
    for (std::size_t axis = 0; axis < midpoints.size(); ++axis) {
      EXPECT_TRUE(HasAxisColorNear(midpoints[axis], camera, size, colors[axis]));
    }
    EXPECT_EQ(glIsEnabled(GL_DEPTH_TEST), GL_TRUE);
    GLboolean depthWrite = GL_FALSE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    EXPECT_EQ(depthWrite, GL_TRUE);
    std::array<GLint, 2> polygonMode{};
    glGetIntegerv(GL_POLYGON_MODE, polygonMode.data());
    EXPECT_EQ(polygonMode[0], wireframe ? GL_LINE : GL_FILL);
    EXPECT_EQ(polygonMode[1], wireframe ? GL_LINE : GL_FILL);
  }
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  renderer.DrawAxisGizmo(camera);
  EXPECT_EQ(glIsEnabled(GL_DEPTH_TEST), GL_FALSE);
  GLboolean depthWrite = GL_TRUE;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
  EXPECT_EQ(depthWrite, GL_FALSE);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(Renderer, FillsTrianglesAndSkipsDegenerateAndZeroSizedFrames)
{
  Jade::Window window(HiddenWindow());
  Jade::Renderer renderer(window, false);
  const Jade::Triangle triangle({1, 1}, {5, 1}, {1, 5}, {1, 0, 0, 1});
  for (const Jade::FramebufferSize requested : {Jade::FramebufferSize{320, 240},
      Jade::FramebufferSize{480, 240}, Jade::FramebufferSize{240, 320}}) {
    glfwSetWindowSize(glfwGetCurrentContext(), requested.width, requested.height);
    window.PollEvents();
    const auto size = window.GetFramebufferSize();
    ASSERT_TRUE(renderer.BeginFrame(size, {}));
    renderer.DrawTriangle(Jade::Triangle{});
    renderer.DrawTriangle(Jade::Triangle{{1, 1}, {2, 2}, {3, 3}});
    EXPECT_TRUE(renderer.ValidateClear({}));
    renderer.DrawTriangle(triangle);
    const auto inside = OverlayPixel({2, 2}, size);
    EXPECT_GT(inside[0], 240);
    EXPECT_LT(inside[1], 10);
    const auto outside = OverlayPixel({5, 5}, size);
    EXPECT_NEAR(outside[0], Jade::Color{}.red * 255, 2);
    EXPECT_NEAR(outside[1], Jade::Color{}.green * 255, 2);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderer.Present();
  }
  EXPECT_FALSE(renderer.BeginFrame({0, 0}, {}));
  renderer.DrawTriangle(triangle);
  renderer.DrawObject(Jade::Object{}, Jade::Camera{});
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(Renderer, WireframeDrawsTriangleEdgesAndKeepsLinesVisible)
{
  Jade::Window window(HiddenWindow());
  Jade::Renderer renderer(window, false);
  const auto size = window.GetFramebufferSize();
  const Jade::Triangle triangle({1, 1}, {5, 1}, {1, 5}, {1, 0, 0, 1});
  ASSERT_TRUE(renderer.BeginFrame(size, {}));
  renderer.DrawTriangle(triangle);
  const auto filled = ColoredBounds(size);
  EXPECT_GT(OverlayPixel({2, 2}, size)[0], 240);

  renderer.setWireframe(true);
  ASSERT_TRUE(renderer.BeginFrame(size, {}));
  renderer.DrawTriangle(triangle);
  const auto wire = ColoredBounds(size);
  EXPECT_GT(wire.coloredPixels, 100);
  EXPECT_LT(wire.coloredPixels, filled.coloredPixels / 5);
  const auto inside = OverlayPixel({2, 2}, size);
  EXPECT_NEAR(inside[0], Jade::Color{}.red * 255, 2);
  EXPECT_NEAR(inside[1], Jade::Color{}.green * 255, 2);
  renderer.DrawLine(Jade::Line{{1, 8}, {5, 8}});
  const int x = size.height * 3 / 10;
  const int y = size.height * 8 / 10;
  bool lineVisible = false;
  for (int offset = -1; offset <= 1; ++offset) {
    const auto pixel = Pixel(x, y + offset);
    lineVisible = lineVisible || (pixel[0] > 240 && pixel[1] > 240 && pixel[2] > 240);
  }
  EXPECT_TRUE(lineVisible);

  renderer.setWireframe(false);
  ASSERT_TRUE(renderer.BeginFrame(size, {}));
  renderer.DrawTriangle(triangle);
  EXPECT_GT(OverlayPixel({2, 2}, size)[0], 240);
  EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(Renderer, WireframeCubeKeepsItsBoundsInBothProjectionModesAndRestoresFill)
{
  Jade::Window window(HiddenWindow());
  Jade::Renderer renderer(window, false);
  Jade::Object cube;
  cube.setMesh(Jade::Mesh::cube(2));
  Jade::Camera camera;
  camera.setPos({0, 0, 5});
  camera.lookAt({0, 0, 0});
  for (auto mode : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
    if (mode == Jade::Projection::Perspective) {
      camera.setPerspective(60);
    } else {
      camera.setOrthographic(6);
    }
    renderer.setWireframe(false);
    const auto filled = DrawObject(renderer, window, cube, camera);
    ASSERT_GT(filled.coloredPixels, 1000);
    renderer.setWireframe(true);
    const auto wire = DrawObject(renderer, window, cube, camera);
    EXPECT_GT(wire.coloredPixels, 100);
    EXPECT_LT(wire.coloredPixels, filled.coloredPixels / 4);
    EXPECT_NEAR(wire.Width(), filled.Width(), 2);
    EXPECT_NEAR(wire.Height(), filled.Height(), 2);
    renderer.setWireframe(false);
    const auto restored = DrawObject(renderer, window, cube, camera);
    EXPECT_EQ(restored.coloredPixels, filled.coloredPixels);
  }
}

TEST(Renderer, CubeRemainsSquareAndCenteredAfterResizeInBothProjectionModes)
{
  Jade::Window window(HiddenWindow());
  Jade::Renderer renderer(window, false);
  Jade::Object cube;
  cube.setMesh(Jade::Mesh::cube(2));
  Jade::Camera camera;
  camera.setPos({0, 0, 5});
  camera.lookAt({0, 0, 0});
  for (auto mode : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
    if (mode == Jade::Projection::Perspective) {
      camera.setPerspective(90);
    } else {
      camera.setOrthographic(6);
    }
    int previousWidth = 0;
    int previousHeight = 0;
    for (const Jade::FramebufferSize requested : {Jade::FramebufferSize{320, 240},
        Jade::FramebufferSize{480, 240}, Jade::FramebufferSize{240, 320}}) {
      glfwSetWindowSize(glfwGetCurrentContext(), requested.width, requested.height);
      window.PollEvents();
      const auto size = window.GetFramebufferSize();
      const auto bounds = DrawObject(renderer, window, cube, camera);
      ASSERT_GT(bounds.Width(), 10);
      EXPECT_NEAR(bounds.Width(), bounds.Height(), 2);
      EXPECT_NEAR((bounds.left + bounds.right) / 2.0, (size.width - 1) / 2.0, 2);
      EXPECT_NEAR((bounds.bottom + bounds.top) / 2.0, (size.height - 1) / 2.0, 2);
      if (previousWidth > 0) {
        EXPECT_NEAR(bounds.Width(), static_cast<double>(previousWidth) * size.height / previousHeight, 2);
      }
      previousWidth = bounds.Width();
      previousHeight = size.height;
      renderer.Present();
    }
  }
}

TEST(Renderer, PerspectiveRespondsToDistanceAndFOVWhileOrthographicKeepsSize)
{
  Jade::Window window(HiddenWindow());
  Jade::Renderer renderer(window, false);
  Jade::Object cube;
  cube.setMesh(Jade::Mesh::cube(2));
  Jade::Camera camera;
  camera.setPerspective(90);
  camera.setPos({0, 0, 4});
  camera.lookAt({0, 0, 0});
  const auto nearBounds = DrawObject(renderer, window, cube, camera);
  camera.setPos({0, 0, 8});
  const auto farBounds = DrawObject(renderer, window, cube, camera);
  ASSERT_GT(farBounds.Width(), 0);
  EXPECT_GT(nearBounds.Width(), farBounds.Width() * 1.8);
  camera.setPos({0, 0, 5});
  camera.setPerspective(30);
  const auto narrowFOV = DrawObject(renderer, window, cube, camera);
  camera.setPerspective(90);
  const auto wideFOV = DrawObject(renderer, window, cube, camera);
  EXPECT_GT(narrowFOV.Width(), wideFOV.Width() * 3.0);

  camera.setOrthographic(6);
  camera.setPos({0, 0, 4});
  const auto orthoNear = DrawObject(renderer, window, cube, camera);
  camera.setPos({0, 0, 8});
  const auto orthoFar = DrawObject(renderer, window, cube, camera);
  EXPECT_GT(orthoFar.Width(), 0);
  EXPECT_NEAR(orthoNear.Width(), orthoFar.Width(), 1);
}

TEST(Renderer, DepthOcclusionIsIndependentOfDrawOrderAndClipPlanesRejectInvisibleCubes)
{
  Jade::Window window(HiddenWindow());
  Jade::Renderer renderer(window, false);
  const auto size = window.GetFramebufferSize();
  Jade::Camera camera;
  camera.setPos({0, 0, 5});
  camera.lookAt({0, 0, 0});
  Jade::Object nearCube;
  nearCube.setMesh(Jade::Mesh::cube(2));
  nearCube.setColor({1, 0, 0, 1});
  Jade::Object farCube;
  farCube.setMesh(Jade::Mesh::cube(2));
  farCube.setPos({0, 0, -2});
  farCube.setColor({0, 0, 1, 1});
  for (auto mode : {Jade::Projection::Perspective, Jade::Projection::Orthographic}) {
    if (mode == Jade::Projection::Perspective) {
      camera.setPerspective(60);
    } else {
      camera.setOrthographic(6);
    }
    for (bool nearFirst : {true, false}) {
      ASSERT_TRUE(renderer.BeginFrame(size, {}));
      renderer.DrawObject(nearFirst ? nearCube : farCube, camera);
      renderer.DrawObject(nearFirst ? farCube : nearCube, camera);
      const auto center = Pixel(size.width / 2, size.height / 2);
      EXPECT_GT(center[0], 200);
      EXPECT_LT(center[2], 10);
      float depth = 1;
      glReadPixels(size.width / 2, size.height / 2, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
      EXPECT_GT(depth, 0);
      EXPECT_LT(depth, 1);
    }
    // A fresh frame must also clear depth; otherwise this farther Cube disappears.
    const auto farOnly = DrawObject(renderer, window, farCube, camera);
    EXPECT_GT(farOnly.Width(), 0);
    const auto blue = Pixel(size.width / 2, size.height / 2);
    EXPECT_LT(blue[0], 10);
    EXPECT_GT(blue[2], 50);

    if (mode == Jade::Projection::Perspective) {
      camera.setPerspective(60, 0.1f, 3);
    } else {
      camera.setOrthographic(6, 0.1f, 3);
    }
    EXPECT_EQ(DrawObject(renderer, window, nearCube, camera).Width(), 0);
    if (mode == Jade::Projection::Perspective) {
      camera.setPerspective(60, 7, 20);
    } else {
      camera.setOrthographic(6, 7, 20);
    }
    EXPECT_EQ(DrawObject(renderer, window, nearCube, camera).Width(), 0);
  }
}
