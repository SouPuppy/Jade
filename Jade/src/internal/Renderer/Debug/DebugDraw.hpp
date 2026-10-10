#pragma once
#include "../Device/OpenGL/OpenGLDevice.hpp"
#include "../Device/OpenGL/OpenGLProgram.hpp"
#include <Jade/Renderer/Common/SceneSnapshot.hpp>
namespace Jade::Detail {
class DebugDraw final {
public:
  explicit DebugDraw(OpenGLDevice&);
  ~DebugDraw();
  void wireframe(const Renderer::SceneSnapshot&, Renderer::ImageSize);
  void axes(const Renderer::CameraFrame&, Renderer::ImageSize);
private:
  void lines(std::span<const Vec3>, const Matrix4&, Vec3, bool);
  OpenGLDevice& m_Device;
  std::unique_ptr<OpenGLProgram> m_LineProgram;
  GLuint m_Vao=0,m_Buffer=0;
};
} // namespace Jade::Detail
