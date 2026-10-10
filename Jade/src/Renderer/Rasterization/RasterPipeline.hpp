#pragma once
#include <Jade/Renderer/Rasterization/Rasterization.hpp>
#include "MaterialCompiler.hpp"
#include "../Common/Emitters.hpp"
#include "../../internal/Renderer/Device/OpenGL/OpenGLDevice.hpp"
#include "../../internal/Renderer/Device/OpenGL/OpenGLProgram.hpp"
#include "../../internal/Acceleration/BVH.hpp"
namespace Jade::Detail {
struct RasterStatistics {
  std::uint64_t frames = 0, emitterBuilds = 0, shadowBuilds = 0, probeCaptures = 0;
};
class RasterPipeline final {
public:
  RasterPipeline(OpenGLDevice &, Renderer::RasterizationConfig);
  ~RasterPipeline();
  void render(const Renderer::SceneSnapshot &, OpenGLFrameTarget &);
  RasterStatistics statistics() const { return m_Stats; }

private:
  struct Light {
    Renderer::EmitterSample sample;
    std::uint32_t instance = 0, primitive = 0;
    std::array<Renderer::CameraFrame, 6> cameras;
    std::array<Matrix4, 6> matrices;
  };
  struct Probe {
    std::size_t instance;
    Vec3 center;
    GLuint raw = 0, filtered = 0;
    bool ready = false;
    ~Probe();
  };
  void release() noexcept;
  float range() const;
  void prepare(const Renderer::SceneSnapshot &);
  void shadows(const Renderer::SceneSnapshot &);
  void probes(const Renderer::SceneSnapshot &);
  void core(const Renderer::SceneSnapshot &, const Renderer::CameraFrame &, OpenGLFrameTarget &, bool capture,
            std::size_t excluded);
  void geometry(const Renderer::RenderInstance &, const RasterMaterial &, const Renderer::CameraFrame &,
                Renderer::ImageSize, OpenGLProgram &);
  void uniforms(OpenGLProgram &, const Renderer::CameraFrame &, Renderer::ImageSize);
  GLuint nearestProbe(Vec3) const;
  void resize(Renderer::ImageSize);
  OpenGLDevice &m_Device;
  Renderer::RasterizationConfig m_Config;
  Renderer::SceneSnapshot m_Previous;
  bool m_Prepared = false;
  Bounds m_Bounds;
  std::vector<Vec3> m_Centers;
  std::vector<RasterMaterial> m_Materials;
  std::vector<Light> m_Lights;
  std::shared_ptr<const EmitterDistribution> m_Emitters;
  std::vector<std::unique_ptr<Probe>> m_Probes;
  std::unique_ptr<OpenGLProgram> m_OpaquePass, m_ShadowPass, m_TransmissionShadowPass, m_BackSurfacePass,
      m_ReflectionPass, m_TransmissionPass, m_ProbePrefilterPass, m_BrdfLutPass;
  OpenGLFrameTarget m_Capture;
  Renderer::ImageSize m_Size;
  GLuint m_Gbuffer = 0, m_Source = 0, m_Positions = 0, m_Normals = 0, m_Depth = 0, m_Transmission = 0;
  GLuint m_BackFbo = 0, m_BackPosition = 0, m_BackNormal = 0, m_BackDepth = 0;
  GLuint m_ShadowFbo = 0, m_GlassShadowFbo = 0, m_ShadowDepth = 0, m_GlassDepth = 0, m_GlassTint = 0;
  GLuint m_LightBuffer = 0, m_LightTexture = 0, m_ProbeFbo = 0, m_Black = 0, m_Brdf = 0, m_Vao = 0;
  RasterStatistics m_Stats;
};
} // namespace Jade::Detail
