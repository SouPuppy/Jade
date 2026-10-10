#include "RenderHost.hpp"
#include "../../Platform/Window.hpp"
#include <Jade/Log.hpp>
#include <chrono>
#include <cmath>
#include <limits>
namespace Jade::Detail {
namespace {
const Log::Logger logger{"Renderer"};
void validate(const Renderer::RenderSettings& s) {
  if(!std::isfinite(s.resolutionScale)||s.resolutionScale<=0||s.resolutionScale>8||!std::isfinite(s.exposureEV)||std::abs(s.exposureEV)>64)
    throw std::invalid_argument("Invalid renderer resolution scale or exposure.");
}
}
RenderHost::RenderHost(Window& w,bool vsync,std::string title)
 : m_Window(w),m_Title(std::move(title)),m_Device(w,vsync),m_Display(m_Device),m_Debug(m_Device),m_Target(m_Device) {}
RenderHost::~RenderHost() { stop();m_Device.current();m_Entries.clear(); }
const Renderer::Backend* RenderHost::active() const noexcept { return m_Entries.empty()?nullptr:m_Entries[m_Active].backend.get(); }
void RenderHost::install(std::type_index type,std::unique_ptr<Renderer::Backend> backend) {
  if(m_Running)throw std::logic_error("Register renderer outside Run.");
  validate(backend->settings());
  auto* progressive=dynamic_cast<Renderer::ProgressiveBackend*>(backend.get());
  if(!progressive && !dynamic_cast<Renderer::FrameBackend*>(backend.get()))throw std::invalid_argument("Backend must implement a frame or progressive contract.");
  std::unique_ptr<ProgressiveSession> session;
  if(progressive) { session=std::make_unique<ProgressiveSession>();session->bind(*progressive); }
  auto it=std::find_if(m_Entries.begin(),m_Entries.end(),[&](const auto& e){return e.type==type;});
  if(it==m_Entries.end())m_Entries.push_back({type,std::move(backend),std::move(session)});
  else { m_Device.current();it->session.reset();*it=Entry{type,std::move(backend),std::move(session)}; }
  invalidate();
}
void RenderHost::select(std::type_index type) {
  auto it=std::find_if(m_Entries.begin(),m_Entries.end(),[&](const auto& e){return e.type==type;});
  if(it==m_Entries.end())throw std::invalid_argument("Renderer type is not registered.");
  m_Pending=static_cast<std::size_t>(it-m_Entries.begin());
  if(!m_Running)applyPending();
}
void RenderHost::cycle() { if(m_Entries.size()>1) { m_Pending=((m_Pending?*m_Pending:m_Active)+1)%m_Entries.size();if(!m_Running)applyPending(); } }
void RenderHost::activate() {
  if(m_Running && !m_Wireframe && m_Entries[m_Active].session) {
    m_Entries[m_Active].session->start();m_ProgressiveRunning=true;
  }
}
void RenderHost::applyPending() {
  if(!m_Pending)return;
  const auto next=*m_Pending;m_Pending.reset();if(next==m_Active)return;
  if(m_ProgressiveRunning) { m_Entries[m_Active].session->stop();m_ProgressiveRunning=false; }
  rethrowError();m_Active=next;invalidate();activate();
  logger<<Log::info("Selected: {}",active()->name());
}
void RenderHost::start() {
  if(m_Entries.empty())throw std::logic_error("Register a renderer with Application::use before Run.");
  stop();invalidate();m_Running=true;
  for(auto& e:m_Entries)e.backend->m_ApplicationActive=true;
  try { activate(); } catch(...) { stop();throw; }
}
void RenderHost::stop() noexcept {
  for(auto& e:m_Entries) { if(e.session)e.session->stop();e.backend->m_ApplicationActive=false; }
  m_Running=false;m_ProgressiveRunning=false;m_Pending.reset();m_Progress.inFlight=false;
}
void RenderHost::invalidate() noexcept {
  for(auto& e:m_Entries)if(e.session)e.session->invalidate();
  m_LastFrame.reset();m_Progress={};
}
void RenderHost::rethrowError() { for(auto& e:m_Entries)if(e.session)e.session->rethrowError(); }
Renderer::RenderProgress RenderHost::progress() const {
  if(active() && !m_Wireframe && m_Entries[m_Active].session) {
    auto result=m_Entries[m_Active].session->progress();result.rendererName=active()->name();return result;
  }
  auto result=m_Progress;if(active())result.rendererName=active()->name();return result;
}
void RenderHost::draw(const Renderer::SceneSnapshot* snapshot,Renderer::ImageSize size,bool wireframe,bool axes) {
  if(m_Wireframe!=wireframe) {
    m_Wireframe=wireframe;invalidate();
    if(m_ProgressiveRunning) { m_Entries[m_Active].session->stop();m_ProgressiveRunning=false; }
    rethrowError();if(!wireframe)activate();
  }
  if(m_FrameSize!=size){invalidate();m_FrameSize=size;}
  m_Window.SetTitle(m_Title+" | "+(wireframe?"Wireframe / ":"")+std::string(active()->name()));
  m_Display.begin(size);
  if(snapshot && snapshot->camera) {
    if(wireframe)m_Debug.wireframe(*snapshot,size);
    else if(auto& session=m_Entries[m_Active].session) {
      session->submit(*snapshot,size);if(auto image=session->image())m_Display.image(*image);
    } else {
      const auto settings=active()->settings();
      const double w=std::lround(size.width*static_cast<double>(settings.resolutionScale)), h=std::lround(size.height*static_cast<double>(settings.resolutionScale));
      if(w>std::numeric_limits<int>::max()||h>std::numeric_limits<int>::max())throw std::invalid_argument("Scaled render dimensions overflow.");
      const Renderer::ImageSize scaled{std::max(1,static_cast<int>(w)),std::max(1,static_cast<int>(h))};
      m_Target.resize(scaled);m_Target.clear({});
      if(!m_LastFrame || !(*m_LastFrame==*snapshot)){m_LastFrame=*snapshot;m_Progress.revision=++m_Revision;}
      const auto started=std::chrono::steady_clock::now();
      auto& backend=static_cast<Renderer::FrameBackend&>(*m_Entries[m_Active].backend);
      backend.render(*snapshot,{scaled,m_Progress.revision,m_Progress.completedFrames},m_Target);
      m_Progress.frameMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
      ++m_Progress.completedFrames;m_Progress.size=scaled;
      glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,size.width,size.height);
      m_Display.hdr(m_Target.color,settings.exposureEV);
    }
    if(axes)m_Debug.axes(*snapshot->camera,size);
  } else invalidate();
  m_Display.present();
}
} // namespace Jade::Detail
