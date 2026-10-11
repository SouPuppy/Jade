#pragma once
#include "../Device/OpenGL/OpenGLDevice.hpp"
#include "../Device/OpenGL/OpenGLProgram.hpp"
#include "CollisionDebug.hpp"
#include <Jade/Renderer/Common/SceneSnapshot.hpp>
#include <Jade/Renderer/RayTracing/RayScene.hpp>
namespace Jade::Detail {
class DebugDraw final {
public:
  explicit DebugDraw(OpenGLDevice &);
  ~DebugDraw();
  void wireframe(const Renderer::SceneSnapshot &, Renderer::ImageSize);
  void axes(const Renderer::CameraFrame &, Renderer::ImageSize);
  void scene(const Renderer::SceneSnapshot&, Renderer::ImageSize, Renderer::DebugViewOptions = {});
  void paths(std::span<const Vec3>, const Renderer::CameraFrame&, Renderer::ImageSize, Vec3 color);
  void frustum(const Renderer::CameraFrame&, const Renderer::CameraFrame&, Renderer::ImageSize);
  void colliders(const CollisionDebugLines &, const Renderer::CameraFrame &, Renderer::ImageSize);

private:
  void lines(std::span<const Vec3>, const Matrix4 &, Vec3, bool);
  OpenGLDevice &m_Device;
  Renderer::RaySceneCompiler m_Compiler;
};
} // namespace Jade::Detail
