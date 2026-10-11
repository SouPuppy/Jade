#pragma once
#include <Jade/Renderer/RenderPipeline.hpp>
#include "../Host/ProgressiveSession.hpp"
#include "../Presentation/OpenGLDisplay.hpp"
namespace Jade::Detail {
class RayTracingPipeline final : public Renderer::RenderPipeline {
public:
  RayTracingPipeline(ProgressiveSession& session,OpenGLDisplay& display,const Renderer::RayTracingSettings& settings)
    :m_Session(session),m_Display(display),m_Settings(settings) {}
  const char* name() const noexcept override {return "RayTracing";}
  void render(Renderer::RenderContext& c) override {
    m_Session.submit(c.scene,c.target.size,c.paused,c.deadline);
    if(!c.paused) {m_Session.waitForFrame();m_Session.rethrowError();}
    if(auto frame=m_Session.gpuFrame())
      m_Display.image(frame,m_Settings.exposureEV,m_Settings.output==Renderer::RayOutput::Normal?
        Renderer::ImageEncoding::LinearData:Renderer::ImageEncoding::Radiance);
    else if(auto image=m_Session.image()) m_Display.image(*image);
  }
private:
  ProgressiveSession& m_Session;
  OpenGLDisplay& m_Display;
  const Renderer::RayTracingSettings& m_Settings;
};
class NonePipeline final : public Renderer::RenderPipeline {
public:
  const char* name() const noexcept override {return "None";}
  void render(Renderer::RenderContext&) override {}
};
}
