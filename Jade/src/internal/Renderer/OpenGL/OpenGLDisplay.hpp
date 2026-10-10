#pragma once

#include <Jade/Renderer/RenderScene.hpp>

#include "../../Platform/Window.hpp"
#include "OpenGLContext.hpp"
#include "OpenGLProgram.hpp"

#include <memory>
#include <span>

namespace Jade::Detail {

// Only image presentation and unlit diagnostic lines. No scene shading.
class OpenGLDisplay final {
public:
  OpenGLDisplay(Window &window, bool vsync);
  ~OpenGLDisplay();
  void begin(Renderer::ImageSize size);
  void image(const Renderer::Image &image);
  void wireframe(const Renderer::SceneSnapshot &snapshot, Renderer::ImageSize size);
  void axes(const Renderer::CameraFrame &camera, Renderer::ImageSize size);
  void present();

private:
  void lines(std::span<const Vec3> points, const Matrix4 &transform, Vec3 color, bool depth);
  OpenGLContext m_Context;
  std::unique_ptr<OpenGLProgram> m_ImageProgram, m_LineProgram;
  GLuint m_Vao = 0, m_Buffer = 0, m_Texture = 0;
  Renderer::ImageSize m_TextureSize;
};

} // namespace Jade::Detail
