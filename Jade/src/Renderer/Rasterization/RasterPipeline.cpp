#include "RasterPipeline.hpp"
#include <JadeShaders.hpp>
#include "../Common/Math.hpp"
#include "../../Graphics/Materials.hpp"
#include "../../internal/Mesh/MeshAccess.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
namespace Jade::Detail {
namespace {
namespace Math = Renderer::Math;
using Renderer::CameraFrame;
using Renderer::ImageSize;
constexpr auto None = std::numeric_limits<std::size_t>::max();
GLint location(OpenGLProgram &p, const char *n) { return glGetUniformLocation(p.Handle(), n); }
void integer(OpenGLProgram &p, const char *n, int v) { glUniform1i(location(p, n), v); }
void scalar(OpenGLProgram &p, const char *n, float v) { glUniform1f(location(p, n), v); }
void vector(OpenGLProgram &p, const char *n, Vec3 v) { glUniform3f(location(p, n), v.x, v.y, v.z); }
void rgb(OpenGLProgram &p, const char *n, Renderer::LinearRGB v)
{
  glUniform3f(location(p, n), v.r, v.g, v.b);
}
void matrix(OpenGLProgram &p, const char *n, const Matrix4 &m)
{
  glUniformMatrix4fv(location(p, n), 1, GL_FALSE, m.data());
}
void texture(OpenGLProgram &p, const char *n, int unit, GLenum kind, GLuint id)
{
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(kind, id);
  integer(p, n, unit);
}
void fullscreen(GLuint vao)
{
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}
void state()
{
  glClearDepth(1);
  glFrontFace(GL_CCW);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_FRAMEBUFFER_SRGB);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}
CameraFrame cubeCamera(Vec3 position, int face, float distance)
{
  static constexpr Vec3 directions[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  static constexpr Vec3 ups[] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
  CameraFrame c;
  c.position = position;
  c.forward = directions[face];
  c.right = Math::unit(cross(c.forward, ups[face]));
  c.up = cross(c.right, c.forward);
  c.lens.fovDegrees = 90;
  c.lens.nearPlane = std::max(distance * 1e-5f, 1e-6f);
  c.lens.farPlane = std::max(distance, c.lens.nearPlane * 2);
  return c;
}
float radical(unsigned value, unsigned base)
{
  float result = 0, fraction = 1.0f / base;
  while (value) {
    result += fraction * (value % base);
    value /= base;
    fraction /= base;
  }
  return result;
}
int mipCount(int size) { return 1 + static_cast<int>(std::floor(std::log2(size))); }
void cube(GLuint &id, int size, bool mips)
{
  GLint maximum;
  glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maximum);
  if (size > maximum)
    throw std::invalid_argument("Probe exceeds GL_MAX_CUBE_MAP_TEXTURE_SIZE.");
  if (!id)
    glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_CUBE_MAP, id);
  for (int level = 0; level < (mips ? mipCount(size) : 1); ++level)
    for (int face = 0; face < 6; ++face)
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGBA32F, std::max(1, size >> level),
                   std::max(1, size >> level), 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}
void array(GLuint &id, int size, int layers, GLint format)
{
  GLint maximum;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
  if (size > maximum)
    throw std::invalid_argument("Shadow map exceeds GL_MAX_TEXTURE_SIZE.");
  if (!id)
    glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D_ARRAY, id);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, format, size, size, layers, 0,
               format == GL_DEPTH_COMPONENT32F ? GL_DEPTH_COMPONENT : GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
bool shadowChanged(const Renderer::SceneSnapshot &a, const Renderer::SceneSnapshot &b)
{
  if (a.instances.size() != b.instances.size())
    return true;
  for (std::size_t i = 0; i < a.instances.size(); ++i) {
    const auto &x = a.instances[i];
    const auto &y = b.instances[i];
    if (x.mesh != y.mesh || x.entity != y.entity || x.objectToWorld.values != y.objectToWorld.values ||
        x.material.model != y.material.model)
      return true;
    if (x.material.model == MaterialModel::Dielectric &&
        (x.material.ior != y.material.ior || x.material.baseColor != y.material.baseColor))
      return true;
  }
  return false;
}
} // namespace
RasterPipeline::Probe::~Probe()
{
  glDeleteTextures(1, &raw);
  glDeleteTextures(1, &filtered);
}
RasterPipeline::RasterPipeline(OpenGLDevice &device, Renderer::RasterizationConfig config)
    : m_Device(device), m_Config(config), m_Capture(device)
{
  try {
    m_Forward = std::make_unique<OpenGLProgram>(Shaders::mesh_vert, Shaders::forward_frag);
    m_Shadow = std::make_unique<OpenGLProgram>(Shaders::mesh_vert, Shaders::shadow_frag);
    m_GlassShadow = std::make_unique<OpenGLProgram>(Shaders::mesh_vert, Shaders::glass_shadow_frag);
    m_Normal = std::make_unique<OpenGLProgram>(Shaders::mesh_vert, Shaders::normal_frag);
    m_Final = std::make_unique<OpenGLProgram>(Shaders::mesh_vert, Shaders::final_frag);
    m_Glass = std::make_unique<OpenGLProgram>(Shaders::mesh_vert, Shaders::glass_frag);
    m_Prefilter = std::make_unique<OpenGLProgram>(Shaders::full_vert, Shaders::prefilter_frag);
    m_BrdfProgram = std::make_unique<OpenGLProgram>(Shaders::full_vert, Shaders::brdf_frag);
    glGenVertexArrays(1, &m_Vao);
    glGenFramebuffers(1, &m_Gbuffer);
    glGenFramebuffers(1, &m_BackFbo);
    glGenFramebuffers(1, &m_ShadowFbo);
    glGenFramebuffers(1, &m_GlassShadowFbo);
    glGenFramebuffers(1, &m_ProbeFbo);
    glGenBuffers(1, &m_LightBuffer);
    glGenTextures(1, &m_LightTexture);
    cube(m_Black, 1, false);
    const float black[4] = {0, 0, 0, 1};
    for (int face = 0; face < 6; ++face)
      glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, 1, 1, GL_RGBA, GL_FLOAT, black);
    Texture2D(m_Brdf, {64, 64}, GL_RGBA16F);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ProbeFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_Brdf, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    CheckFramebuffer();
    glViewport(0, 0, 64, 64);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(m_BrdfProgram->Handle());
    fullscreen(m_Vao);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  } catch (...) {
    release();
    throw;
  }
}
void RasterPipeline::release() noexcept
{
  const GLuint textures[] = {m_Source,       m_Positions,    m_Normals,   m_Depth,       m_Transmission,
                             m_BackPosition, m_BackNormal,   m_BackDepth, m_ShadowDepth, m_GlassDepth,
                             m_GlassTint,    m_LightTexture, m_Black,     m_Brdf};
  glDeleteTextures(static_cast<GLsizei>(std::size(textures)), textures);
  const GLuint fbos[] = {m_Gbuffer, m_BackFbo, m_ShadowFbo, m_GlassShadowFbo, m_ProbeFbo};
  glDeleteFramebuffers(static_cast<GLsizei>(std::size(fbos)), fbos);
  glDeleteBuffers(1, &m_LightBuffer);
  glDeleteVertexArrays(1, &m_Vao);
}
RasterPipeline::~RasterPipeline() { release(); }
float RasterPipeline::range() const
{
  return m_Bounds.valid() ? std::max(Math::length(m_Bounds.high - m_Bounds.low) * 2, 0.01f) : 1;
}
void RasterPipeline::prepare(const Renderer::SceneSnapshot &snapshot)
{
  if (snapshot.camera)
    snapshot.camera->generateRay({.5f, .5f}, {1, 1});
  const bool worldChanged = !m_Prepared || snapshot.instances != m_Previous.instances;
  const bool emitChanged = !m_Prepared || !sameEmitters(snapshot, m_Previous);
  const bool depthChanged = !m_Prepared || emitChanged || shadowChanged(snapshot, m_Previous);
  if (worldChanged) {
    m_Bounds = {};
    m_Centers.clear();
    for (const auto &instance : snapshot.instances) {
      if (!instance.mesh)
        throw std::invalid_argument("Raster geometry requires an immutable mesh.");
      Renderer::Math::inverseAffine(instance.objectToWorld);
      Renderer::validateMaterial(instance.material);
      const auto box = MeshAccess::acceleration(*instance.mesh).bounds();
      Bounds transformed;
      if (box.valid())
        for (int corner = 0; corner < 8; ++corner)
          transformed.add(Math::point(instance.objectToWorld, {corner & 1 ? box.high.x : box.low.x,
                                                               corner & 2 ? box.high.y : box.low.y,
                                                               corner & 4 ? box.high.z : box.low.z}));
      m_Bounds.add(transformed);
      m_Centers.push_back(transformed.valid() ? transformed.center()
                                              : Math::point(instance.objectToWorld, {}));
    }
    for (auto &probe : m_Probes)
      probe->ready = false;
  }
  if (emitChanged) {
    m_Emitters = compileEmitters(snapshot);
    ++m_Stats.emitterBuilds;
    m_Lights.clear();
    if (m_Emitters->totalWeight > 0)
      for (unsigned i = 0; i < m_Config.areaSamples; ++i) {
        const float selection = (i + .5f) / m_Config.areaSamples;
        Light l;
        l.sample = m_Emitters->sample(selection, {radical(i + 1, 2), radical(i + 1, 3)});
        const auto it = std::upper_bound(m_Emitters->emitters.begin(), m_Emitters->emitters.end(),
                                         selection * m_Emitters->totalWeight,
                                         [](double v, const Emitter &e) { return v < e.cumulative; });
        const auto &emitter = it == m_Emitters->emitters.end() ? m_Emitters->emitters.back() : *it;
        l.instance = emitter.instance;
        l.primitive = emitter.primitive;
        m_Lights.push_back(l);
      }
  }
  if (depthChanged) {
    std::vector<float> data;
    data.reserve(std::max<std::size_t>(1, m_Lights.size()) * 108);
    for (auto &l : m_Lights) {
      const auto p = l.sample.position;
      data.insert(data.end(), {p.x, p.y, p.z, 1, l.sample.normal.x, l.sample.normal.y, l.sample.normal.z,
                               l.sample.twoSided ? 1.0f : 0.0f});
      if (!(l.sample.areaPdf > 0))
        throw std::invalid_argument("Emitter PDF exceeds raster precision.");
      const auto weighted = l.sample.emission * (1 / (m_Config.areaSamples * l.sample.areaPdf));
      if (!weighted.finite())
        throw std::invalid_argument("Raster emitter radiance exceeds floating-point range.");
      data.insert(data.end(), {weighted.r, weighted.g, weighted.b, 0});
      // The originating primitive is excluded in depth passes, leaving the remaining emitting mesh as an
      // occluder.
      for (int f = 0; f < 6; ++f) {
        l.cameras[f] = cubeCamera(p, f, range());
        l.matrices[f] = l.cameras[f].viewProjection({1, 1});
        data.insert(data.end(), l.matrices[f].values.begin(), l.matrices[f].values.end());
      }
    }
    if (data.empty())
      data.resize(108, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, m_LightBuffer);
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(),
                 GL_STATIC_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, m_LightTexture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_LightBuffer);
    GLint layers;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &layers);
    const int needed = static_cast<int>(std::max<std::size_t>(1, m_Lights.size()) * 6);
    if (needed > layers)
      throw std::runtime_error("Insufficient OpenGL shadow array layers.");
    const int r = m_Config.shadows ? m_Config.shadowResolution : 1;
    array(m_ShadowDepth, r, needed, GL_DEPTH_COMPONENT32F);
    array(m_GlassDepth, r, needed, GL_DEPTH_COMPONENT32F);
    array(m_GlassTint, r, needed, GL_RGBA16F);
    if (m_Config.shadows && !m_Lights.empty())
      shadows(snapshot);
  }
  m_Previous = snapshot;
  m_Prepared = true;
}
void RasterPipeline::uniforms(OpenGLProgram &p, const CameraFrame &camera, ImageSize size)
{
  glUseProgram(p.Handle());
  matrix(p, "u_VP", camera.viewProjection(size));
  vector(p, "u_Eye", camera.position);
  vector(p, "u_Forward", camera.forward);
  integer(p, "u_Perspective", camera.lens.projection == Projection::Perspective);
  glUniform2f(location(p, "u_Size"), static_cast<float>(size.width), static_cast<float>(size.height));
  integer(p, "u_LightCount", static_cast<int>(m_Lights.size()));
  integer(p, "u_Shadows", m_Config.shadows);
  glUniform2f(location(p, "u_ShadowTexel"), 1.0f / m_Config.shadowResolution,
              1.0f / m_Config.shadowResolution);
  texture(p, "u_Lights", 0, GL_TEXTURE_BUFFER, m_LightTexture);
  texture(p, "u_ShadowDepth", 1, GL_TEXTURE_2D_ARRAY, m_ShadowDepth);
  texture(p, "u_GlassDepth", 2, GL_TEXTURE_2D_ARRAY, m_GlassDepth);
  texture(p, "u_GlassTint", 3, GL_TEXTURE_2D_ARRAY, m_GlassTint);
  texture(p, "u_Source", 4, GL_TEXTURE_2D, m_Source);
  texture(p, "u_Positions", 5, GL_TEXTURE_2D, m_Positions);
  texture(p, "u_BackPosition", 6, GL_TEXTURE_2D, m_BackPosition);
  texture(p, "u_BackNormal", 7, GL_TEXTURE_2D, m_BackNormal);
  texture(p, "u_Probe", 8, GL_TEXTURE_CUBE_MAP, m_Black);
  texture(p, "u_Brdf", 9, GL_TEXTURE_2D, m_Brdf);
  scalar(p, "u_ProbeMip", static_cast<float>(mipCount(m_Config.reflectionProbeResolution) - 1));
  scalar(p, "u_Range", range());
  scalar(p, "u_Thickness", range() * .002f);
  integer(p, "u_ReflectionSteps", m_Config.reflectionSteps);
  integer(p, "u_RefractionSteps", m_Config.refractionSteps);
}
void RasterPipeline::geometry(const Renderer::RenderInstance &instance, const CameraFrame &camera,
                              ImageSize size, OpenGLProgram &p)
{
  matrix(p, "u_Model", instance.objectToWorld);
  matrix(p, "u_VP", camera.viewProjection(size));
  const auto inverse = Math::inverseAffine(instance.objectToWorld);
  Matrix4 normal = Matrix4::Identity();
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 3; ++col)
      normal.at(row, col) = inverse.at(col, row);
  matrix(p, "u_Normal", normal);
  vector(p, "u_Eye", camera.position);
  vector(p, "u_Forward", camera.forward);
  integer(p, "u_Perspective", camera.lens.projection == Projection::Perspective);
  const auto &m = instance.material;
  rgb(p, "u_BaseColor", m.baseColor);
  rgb(p, "u_Emission", m.emission);
  integer(p, "u_ModelKind", static_cast<int>(m.model));
  scalar(p, "u_Roughness", m.roughness);
  scalar(p, "u_Ior", m.ior);
  integer(p, "u_TwoSided", m.twoSidedEmission);
  m_Device.mesh(instance.mesh).draw();
}
void RasterPipeline::shadows(const Renderer::SceneSnapshot &snapshot)
{
  const ImageSize size{static_cast<int>(m_Config.shadowResolution),
                       static_cast<int>(m_Config.shadowResolution)};
  state();
  glViewport(0, 0, size.width, size.height);
  for (std::size_t i = 0; i < m_Lights.size(); ++i)
    for (int face = 0; face < 6; ++face) {
      const auto &l = m_Lights[i];
      const int layer = static_cast<int>(i) * 6 + face;
      glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFbo);
      glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_ShadowDepth, 0, layer);
      glDrawBuffer(GL_NONE);
      glReadBuffer(GL_NONE);
      CheckFramebuffer();
      glClearDepth(1);
      glClear(GL_DEPTH_BUFFER_BIT);
      glUseProgram(m_Shadow->Handle());
      for (std::size_t j = 0; j < snapshot.instances.size(); ++j)
        if (snapshot.instances[j].material.model != MaterialModel::Dielectric) {
          integer(*m_Shadow, "u_SkipPrimitive", j == l.instance ? static_cast<int>(l.primitive) : -1);
          geometry(snapshot.instances[j], l.cameras[face], size, *m_Shadow);
        }
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_GlassShadowFbo);
      glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_GlassDepth, 0, layer);
      glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_GlassTint, 0, layer);
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
      CheckFramebuffer();
      glBindFramebuffer(GL_READ_FRAMEBUFFER, m_ShadowFbo);
      glBlitFramebuffer(0, 0, size.width, size.height, 0, 0, size.width, size.height, GL_DEPTH_BUFFER_BIT,
                        GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, m_GlassShadowFbo);
      const GLfloat clear[] = {1, 1, 1, 1};
      glClearBufferfv(GL_COLOR, 0, clear);
      glUseProgram(m_GlassShadow->Handle());
      for (std::size_t j = 0; j < snapshot.instances.size(); ++j)
        if (snapshot.instances[j].material.model == MaterialModel::Dielectric) {
          integer(*m_GlassShadow, "u_SkipPrimitive", j == l.instance ? static_cast<int>(l.primitive) : -1);
          geometry(snapshot.instances[j], l.cameras[face], size, *m_GlassShadow);
        }
    }
  ++m_Stats.shadowBuilds;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
GLuint RasterPipeline::nearestProbe(Vec3 center) const
{
  const Probe *selected = nullptr;
  float distance = INFINITY;
  for (const auto &p : m_Probes)
    if (p->ready) {
      const auto delta = p->center - center;
      const float d = dot(delta, delta);
      if (d < distance) {
        distance = d;
        selected = p.get();
      }
    }
  return selected ? selected->filtered : m_Black;
}
void RasterPipeline::resize(ImageSize size)
{
  if (size == m_Size)
    return;
  m_Size = size;
  Texture2D(m_Source, size, GL_RGBA32F);
  Texture2D(m_Positions, size, GL_RGBA32F);
  Texture2D(m_Normals, size, GL_RGBA16F);
  Texture2D(m_Depth, size, GL_DEPTH_COMPONENT32F);
  Texture2D(m_Transmission, size, GL_RGBA32F);
  Texture2D(m_BackPosition, size, GL_RGBA32F);
  Texture2D(m_BackNormal, size, GL_RGBA16F);
  Texture2D(m_BackDepth, size, GL_DEPTH_COMPONENT32F);
  glBindFramebuffer(GL_FRAMEBUFFER, m_Gbuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_Source, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_Positions, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_Normals, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_Depth, 0);
  const GLenum draw[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
  glDrawBuffers(3, draw);
  CheckFramebuffer();
  glBindFramebuffer(GL_FRAMEBUFFER, m_BackFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_BackPosition, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_BackNormal, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_BackDepth, 0);
  glDrawBuffers(2, draw);
  CheckFramebuffer();
}
void RasterPipeline::core(const Renderer::SceneSnapshot &snapshot, const CameraFrame &camera,
                          OpenGLFrameTarget &target, bool capture, std::size_t excluded)
{
  const auto size = target.size();
  resize(size);
  state();
  glViewport(0, 0, size.width, size.height);
  glBindFramebuffer(GL_FRAMEBUFFER, m_Gbuffer);
  glClearColor(0, 0, 0, 0);
  glClearDepth(1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  uniforms(*m_Forward, camera, size);
  for (std::size_t i = 0; i < snapshot.instances.size(); ++i)
    if (i != excluded && snapshot.instances[i].material.model != MaterialModel::Dielectric)
      geometry(snapshot.instances[i], camera, size, *m_Forward);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, m_Source);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  target.clear({});
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Gbuffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.framebuffer);
  glBlitFramebuffer(0, 0, size.width, size.height, 0, 0, size.width, size.height, GL_DEPTH_BUFFER_BIT,
                    GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  uniforms(*m_Final, camera, size);
  integer(*m_Final, "u_Reflections", !capture && m_Config.reflections);
  for (std::size_t i = 0; i < snapshot.instances.size(); ++i)
    if (i != excluded && snapshot.instances[i].material.model != MaterialModel::Dielectric) {
      texture(*m_Final, "u_Probe", 8, GL_TEXTURE_CUBE_MAP, nearestProbe(m_Centers[i]));
      geometry(snapshot.instances[i], camera, size, *m_Final);
    }
  std::vector<std::size_t> glass;
  for (std::size_t i = 0; i < snapshot.instances.size(); ++i)
    if (i != excluded && snapshot.instances[i].material.model == MaterialModel::Dielectric)
      glass.push_back(i);
  std::stable_sort(glass.begin(), glass.end(), [&](auto a, auto b) {
    const auto x = m_Centers[a] - camera.position, y = m_Centers[b] - camera.position;
    return dot(x, x) > dot(y, y);
  });
  for (const auto i : glass) {
    if (!capture) {
      glBindFramebuffer(GL_FRAMEBUFFER, m_BackFbo);
      glDepthMask(GL_TRUE);
      glClearColor(0, 0, 0, 0);
      glClearDepth(1);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glDepthMask(GL_TRUE);
      glDepthFunc(GL_LESS);
      glEnable(GL_CULL_FACE);
      glCullFace(GL_FRONT);
      glUseProgram(m_Normal->Handle());
      geometry(snapshot.instances[i], camera, size, *m_Normal);
      glDisable(GL_CULL_FACE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_Transmission);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, size.width, size.height);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    uniforms(*m_Glass, camera, size);
    texture(*m_Glass, "u_Source", 4, GL_TEXTURE_2D, m_Transmission);
    texture(*m_Glass, "u_Probe", 8, GL_TEXTURE_CUBE_MAP, nearestProbe(m_Centers[i]));
    integer(*m_Glass, "u_Capture", capture);
    integer(*m_Glass, "u_Reflections", !capture && m_Config.reflections);
    integer(*m_Glass, "u_Refractions", !capture && m_Config.refractions);
    geometry(snapshot.instances[i], camera, size, *m_Glass);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
}
void RasterPipeline::probes(const Renderer::SceneSnapshot &snapshot)
{
  if (!m_Config.reflections || !m_Config.maxReflectionProbes) {
    m_Probes.clear();
    return;
  }
  std::vector<std::size_t> selected;
  for (std::size_t i = 0; i < snapshot.instances.size(); ++i)
    if (snapshot.instances[i].material.model != MaterialModel::Diffuse)
      selected.push_back(i);
  std::stable_sort(selected.begin(), selected.end(), [&](auto a, auto b) {
    const auto x = m_Centers[a] - snapshot.camera->position, y = m_Centers[b] - snapshot.camera->position;
    const auto dx = dot(x, x), dy = dot(y, y);
    return dx == dy ? snapshot.instances[a].entity < snapshot.instances[b].entity : dx < dy;
  });
  if (selected.size() > m_Config.maxReflectionProbes)
    selected.resize(m_Config.maxReflectionProbes);
  std::vector<std::unique_ptr<Probe>> next;
  for (auto i : selected) {
    auto it =
        std::find_if(m_Probes.begin(), m_Probes.end(), [&](const auto &p) { return p && p->instance == i; });
    if (it != m_Probes.end())
      next.push_back(std::move(*it));
    else {
      auto p = std::make_unique<Probe>();
      p->instance = i;
      next.push_back(std::move(p));
    }
    next.back()->center = m_Centers[i];
  }
  m_Probes = std::move(next);
  const int r = m_Config.reflectionProbeResolution;
  m_Capture.resize({r, r});
  for (auto &p : m_Probes)
    if (!p->ready) {
      cube(p->raw, r, true);
      cube(p->filtered, r, true);
      for (int face = 0; face < 6; ++face) {
        const auto c = cubeCamera(p->center, face, range());
        core(snapshot, c, m_Capture, true, p->instance);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Capture.framebuffer);
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_CUBE_MAP, p->raw);
        glCopyTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, 0, 0, r, r);
      }
      glBindTexture(GL_TEXTURE_CUBE_MAP, p->raw);
      glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
      glBindFramebuffer(GL_FRAMEBUFFER, m_ProbeFbo);
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
      glDisable(GL_DEPTH_TEST);
      glUseProgram(m_Prefilter->Handle());
      texture(*m_Prefilter, "u_Probe", 8, GL_TEXTURE_CUBE_MAP, p->raw);
      for (int mip = 0; mip < mipCount(r); ++mip)
        for (int face = 0; face < 6; ++face) {
          glViewport(0, 0, std::max(1, r >> mip), std::max(1, r >> mip));
          glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                 p->filtered, mip);
          CheckFramebuffer();
          const auto c = cubeCamera({}, face, range());
          vector(*m_Prefilter, "u_Direction", c.forward);
          vector(*m_Prefilter, "u_Right", c.right);
          vector(*m_Prefilter, "u_Up", c.up);
          scalar(*m_Prefilter, "u_Roughness", static_cast<float>(mip) / (mipCount(r) - 1));
          fullscreen(m_Vao);
        }
      p->ready = true;
      ++m_Stats.probeCaptures;
    }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
void RasterPipeline::render(const Renderer::SceneSnapshot &snapshot, OpenGLFrameTarget &target)
{
  m_Device.current();
  if (!snapshot.camera) {
    target.clear({});
    return;
  }
  prepare(snapshot);
  probes(snapshot);
  core(snapshot, *snapshot.camera, target, false, None);
  ++m_Stats.frames;
}
} // namespace Jade::Detail
