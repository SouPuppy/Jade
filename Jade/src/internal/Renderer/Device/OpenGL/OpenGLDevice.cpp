#include "OpenGLDevice.hpp"
#include "../../../Platform/Window.hpp"
#include <stdexcept>
#include "RasterScene.hpp"
#include "OpenGLProgram.hpp"
#include <JadeShaders.hpp>
namespace Jade::Detail {
OpenGLDevice::OpenGLDevice(Window &window, bool vsync, JobSystem* jobs)
    : m_Context(static_cast<GLFWwindow *>(window.NativeHandle()), vsync), m_Jobs(jobs)
{
}
OpenGLDevice::~OpenGLDevice() {
  current(); m_Raster.reset(); m_Lines.reset();
  glDeleteBuffers(1,&m_LineBuffer); glDeleteVertexArrays(1,&m_LineVao);
}
void OpenGLDevice::drawScene(const Renderer::SceneSnapshot& s,Renderer::RenderTarget t,Renderer::PipelineSettings p,bool rayShadows) {
  current(); if(!m_Raster) m_Raster=std::make_unique<RasterScene>(m_Jobs);
  m_Raster->draw(s,t,p,rayShadows);
}
void OpenGLDevice::prepareScene(const Renderer::SceneSnapshot& s,Renderer::RenderTarget t,bool rayShadows) {
  current();if(!m_Raster) m_Raster=std::make_unique<RasterScene>(m_Jobs);m_Raster->prepare(s,t,rayShadows);
}
void OpenGLDevice::drawShadows(const Renderer::SceneSnapshot& s,bool rayShadows) {current();m_Raster->shadows(s,rayShadows);}
void OpenGLDevice::drawGeometry(const Renderer::SceneSnapshot& s,Renderer::PipelineSettings p) {current();m_Raster->geometry(s,p);}
void OpenGLDevice::composite(Renderer::PipelineSettings p) {current();m_Raster->composite(p);}
void OpenGLDevice::drawLines(std::span<const Vec3> points,const Matrix4& transform,Vec3 color,bool depth) {
  if(points.empty()) return;
  current();
  if(!m_Lines) {
    m_Lines=std::make_unique<OpenGLProgram>(Shaders::LinesVertex,Shaders::LinesFragment);
    glGenVertexArrays(1,&m_LineVao);glGenBuffers(1,&m_LineBuffer);
    glBindVertexArray(m_LineVao);glBindBuffer(GL_ARRAY_BUFFER,m_LineBuffer);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vec3),nullptr);
  }
  glUseProgram(m_Lines->Handle());glBindVertexArray(m_LineVao);glBindBuffer(GL_ARRAY_BUFFER,m_LineBuffer);
  glUniformMatrix4fv(m_Lines->Uniform("u_Transform"),1,GL_FALSE,transform.data());
  glUniform3f(m_Lines->Uniform("u_Color"),color.x,color.y,color.z);
  if(depth) glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);
  constexpr std::size_t batch=65536;
  for(std::size_t offset=0;offset<points.size();offset+=batch) {
    const auto count=std::min(batch,points.size()-offset);
    glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(count*sizeof(Vec3)),points.data()+offset,GL_STREAM_DRAW);
    glDrawArrays(GL_LINES,0,static_cast<GLsizei>(count));
  }
  glDepthMask(GL_TRUE);glDisable(GL_DEPTH_TEST);
}
void OpenGLDevice::current() const
{
  if (std::this_thread::get_id() != m_Owner)
    throw std::logic_error("OpenGL device used from a foreign thread.");
  m_Context.MakeCurrent();
}
void OpenGLDevice::present() const
{
  current();
  m_Context.Present();
}
} // namespace Jade::Detail
