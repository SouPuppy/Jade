#include <Jade/Renderer/RayTracing/ComputeRayTracer.hpp>
#include "../../../../Renderer/RayTracing/Settings.hpp"
#include "../../../../Renderer/RayTracing/Emitters.hpp"
#include <Jade/Log.hpp>
#include "../../../../Renderer/Common/Math.hpp"
#include "OpenGLComputeDevice.hpp"
#include "GpuFrame.hpp"
#include "OpenGLProgram.hpp"
#include "../../RaySceneAccess.hpp"
#include "../../../Mesh/MeshAccess.hpp"
#include "JadeShaders.hpp"
#include <array>
#include <mutex>
#include <unordered_map>

namespace Jade::Renderer {
namespace {
using namespace Jade::Detail;
constexpr std::uint32_t Invalid = UINT32_MAX;
struct alignas(16) Float4 { float x = 0, y = 0, z = 0, w = 0; };
struct GpuNode { Float4 low, high; std::array<std::uint32_t, 4> links; };
struct GpuTriangle { Float4 a, b, c, na, nb, nc; };
struct GpuInstance { Matrix4 model, inverse; std::array<std::uint32_t, 4> mesh{}; };
struct GpuMaterial { Float4 color, emission, properties, region; std::array<std::uint32_t, 4> texture{}; };
struct GpuEmitter { Float4 a, b, c, normal, emission; };
struct GpuLight { Float4 position,direction,intensity,right,up; };
static_assert(sizeof(GpuNode) == 48 && sizeof(GpuTriangle) == 96 && sizeof(GpuInstance) == 144 &&
              sizeof(GpuMaterial) == 80 && sizeof(GpuEmitter) == 80);
Float4 pack(Vec3 value) { return {value.x, value.y, value.z, 0}; }
Float4 pack(LinearRGB value, float w = 0) { return {value.r, value.g, value.b, w}; }
std::uint32_t indexOf(std::size_t index)
{
  if (index >= Invalid) throw std::overflow_error("GPU scene exceeds the supported index range.");
  return static_cast<std::uint32_t>(index);
}
void appendTree(const BVH &tree, std::vector<GpuNode> &nodes, std::vector<std::uint32_t> &order)
{
  const auto nodeOffset = indexOf(nodes.size()), orderOffset = indexOf(order.size());
  indexOf(nodes.size() + tree.nodes().size());
  indexOf(order.size() + tree.order().size());
  for (const auto &node : tree.nodes()) {
    auto low = node.bounds.low, high = node.bounds.high;
    // Conservative bounds accommodate float shader roundoff at shared edges.
    low = {std::nextafter(low.x, -INFINITY), std::nextafter(low.y, -INFINITY), std::nextafter(low.z, -INFINITY)};
    high = {std::nextafter(high.x, INFINITY), std::nextafter(high.y, INFINITY), std::nextafter(high.z, INFINITY)};
    nodes.push_back({pack(low), pack(high),
      {node.start + orderOffset, node.count, node.left + nodeOffset, node.right + nodeOffset}});
  }
  order.insert(order.end(), tree.order().begin(), tree.order().end());
}
void checkGl()
{
  if (const auto error = glGetError(); error != GL_NO_ERROR)
    throw std::runtime_error("OpenGL compute operation failed with error " + std::to_string(error));
}
} // namespace

struct ComputeRayTracer::Impl {
  RayTracingSettings config;
  OpenGLComputeDevice device;
  std::unique_ptr<OpenGLProgram> program;
  std::array<GLuint, 10> buffers{};
  GLuint texture = 0, accumulation = 0;
  ImageSize textureSize, accumulationSize;
  bool shared;
  std::array<std::shared_ptr<GpuFrame>, 3> frames;
  std::uint32_t emitterCount = 0, lightCount = 0;
  GLint64 maximumBufferBytes = 0;
  GLint maximumTextureSize = 0;
  std::mutex renderMutex;
  RayScene previous;
  bool uploaded = false;
  std::vector<RenderInstance> layout;
  std::vector<std::shared_ptr<const Texture>> textureLayout;
  std::unordered_map<const Texture*, std::uint32_t> textureOffsets;
  struct MeshRange { std::uint32_t root, triangle; };
  std::unordered_map<const void *, MeshRange> meshes;
  std::vector<GpuNode> blasNodes;
  std::vector<std::uint32_t> blasOrder;
  std::uint32_t tlasRoot = Invalid;
  std::array<GLint, 18> uniforms{};

  explicit Impl(RayTracingSettings input, bool shareWithCurrent) : config(input), device(shareWithCurrent), shared(shareWithCurrent)
  {
    ComputeContextScope context(device);
    try {
      program = std::make_unique<OpenGLProgram>(Shaders::RayTraceCompute);
      glGetInteger64v(0x90DE /* GL_MAX_SHADER_STORAGE_BLOCK_SIZE */, &maximumBufferBytes);
      glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
      const char *names[] = {"imageSize", "cameraPosition", "cameraRight", "cameraUp", "cameraForward",
                            "lens", "tlasRoot", "normalShading", "jitter", "seed", "sampleIndex", "emitterCount",
                            "maxBounces", "rrStartBounce", "pathTracing", "accumulate", "sampleWeight", "lightCount"};
      for (std::size_t i = 0; i < std::size(names); ++i) uniforms[i] = program->Uniform(names[i]);
      glGenBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
      glGenTextures(1, &texture);
      glGenTextures(1, &accumulation);
      for (auto& frame : frames) { frame = std::make_shared<GpuFrame>(); glGenTextures(1, &frame->texture); }
      checkGl();
    } catch (...) {
      glDeleteTextures(1, &texture);
      glDeleteTextures(1, &accumulation);
      for (auto& frame : frames) if (frame) glDeleteTextures(1, &frame->texture);
      glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
      program.reset();
      throw;
    }
    const Log::Logger logger{"Renderer/Compute"};
    logger << Log::info("GPU ray/path tracing: {}", device.name());
  }
  ~Impl()
  {
    ComputeContextScope context(device);
    glFinish();
    glDeleteTextures(1, &texture);
    glDeleteTextures(1, &accumulation);
    for (auto& frame : frames) {
      if (frame->ready) glDeleteSync(frame->ready);
      if (const auto consumed = frame->consumed.exchange(nullptr)) glDeleteSync(consumed);
      glDeleteTextures(1, &frame->texture);
    }
    glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
    program.reset();
  }
  template<class T> void upload(std::size_t binding, const std::vector<T> &data)
  {
    const auto bytes = std::max(sizeof(T), data.size() * sizeof(T));
    if (bytes > static_cast<std::uint64_t>(maximumBufferBytes) ||
        bytes > static_cast<std::uint64_t>(std::numeric_limits<GLsizeiptr>::max()))
      throw std::invalid_argument("GPU scene buffer exceeds the OpenGL SSBO size limit.");
    glBindBuffer(ShaderStorageBuffer, buffers[binding]);
    glBufferData(ShaderStorageBuffer, static_cast<GLsizeiptr>(bytes), data.empty() ? nullptr : data.data(), GL_STATIC_DRAW);
    glBindBufferBase(ShaderStorageBuffer, static_cast<GLuint>(binding), buffers[binding]);
  }
  void uploadScene(const RayScene &scene, std::stop_token cancellation)
  {
    if (uploaded && RaySceneAccess::identity(scene) == RaySceneAccess::identity(previous)) return;
    const auto instances = RaySceneAccess::instances(scene);
    bool sameLayout = uploaded && instances.size() == layout.size();
    for (std::size_t i = 0; sameLayout && i < instances.size(); ++i)
      sameLayout = MeshAccess::identity(*instances[i].mesh) == MeshAccess::identity(*layout[i].mesh);
    const bool sameGeometry = sameLayout && RaySceneAccess::geometryIdentity(scene) == RaySceneAccess::geometryIdentity(previous);
    const bool sameLighting = uploaded && RaySceneAccess::emitterIdentity(scene) == RaySceneAccess::emitterIdentity(previous);
    // Failed/cancelled uploads must never leave a cache that appears current.
    uploaded = false;
    if (!sameLayout) {
      meshes.clear(); blasNodes.clear(); blasOrder.clear();
      std::vector<GpuTriangle> triangles;
      for (const auto &instance : instances) {
        CheckCancellation(cancellation);
        const auto identity = MeshAccess::identity(*instance.mesh);
        if (meshes.contains(identity)) continue;
        const auto &tree = MeshAccess::acceleration(*instance.mesh);
        MeshRange range{tree.nodes().empty() ? Invalid : indexOf(blasNodes.size()), indexOf(triangles.size())};
        const auto &mesh = instance.mesh->data();
        indexOf(triangles.size() + mesh.indices.size() / 3);
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
          if ((i & 1023) == 0) CheckCancellation(cancellation);
          const auto &a = mesh.vertices[mesh.indices[i]], &b = mesh.vertices[mesh.indices[i + 1]], &c = mesh.vertices[mesh.indices[i + 2]];
          triangles.push_back({pack(a.position), pack(b.position), pack(c.position), pack(a.normal), pack(b.normal), pack(c.normal)});
        }
        appendTree(tree, blasNodes, blasOrder);
        meshes.emplace(identity, range);
      }
      upload(2, triangles);
    }
    if (!sameGeometry) {
      auto nodes = blasNodes;
      auto order = blasOrder;
      const auto &tree = RaySceneAccess::acceleration(scene);
      tlasRoot = tree.nodes().empty() ? Invalid : indexOf(nodes.size());
      appendTree(tree, nodes, order);
      upload(0, nodes); upload(1, order);
    }
    if (!sameGeometry || !sameLighting) {
      const auto& lighting = RaySceneAccess::emitters(scene);
      std::vector<GpuEmitter> emitters;
      for (const auto& e : lighting.emitters) {
        CheckCancellation(cancellation);
        auto a = pack(e.a); a.w = static_cast<float>(e.weight / lighting.totalWeight / e.area);
        auto b = pack(e.b); b.w = static_cast<float>(e.cumulative / lighting.totalWeight);
        auto normal = pack(e.normal); normal.w = e.twoSided ? 1.0f : 0.0f;
        emitters.push_back({a, b, pack(e.c), normal, pack(e.emission)});
      }
      emitterCount = indexOf(emitters.size());
      if (!emitters.empty()) emitters.back().b.w = 1;
      upload(5, emitters);
      std::vector<GpuInstance> gpuInstances;
      std::vector<std::uint32_t> emitterMap;
      for (std::size_t i = 0; i < instances.size(); ++i) {
        CheckCancellation(cancellation);
        const auto& instance = instances[i];
        const auto range = meshes.at(MeshAccess::identity(*instance.mesh));
        auto offset = Invalid;
        if (!lighting.emitterIndices[i].empty()) {
          offset = indexOf(emitterMap.size());
          indexOf(emitterMap.size() + instance.mesh->data().indices.size() / 3);
          emitterMap.resize(emitterMap.size() + instance.mesh->data().indices.size() / 3, Invalid);
          for (const auto& [primitive, emitter] : lighting.emitterIndices[i]) emitterMap[offset + primitive] = indexOf(emitter);
        }
        gpuInstances.push_back({instance.objectToWorld, Math::inverseAffine(instance.objectToWorld), {range.root, range.triangle, offset, 0}});
      }
      upload(3, gpuInstances); upload(6, emitterMap);
    }
    std::vector<std::shared_ptr<const Texture>> textures;
    for (const auto& instance : instances)
      if (instance.texture && std::find(textures.begin(), textures.end(), instance.texture) == textures.end())
        textures.push_back(instance.texture);
    if (textures != textureLayout) {
      std::vector<std::uint32_t> texels;
      textureOffsets.clear();
      for (const auto& source : textures) {
        textureOffsets.emplace(source.get(), indexOf(texels.size()));
        const auto& rgba = source->pixels();
        for (std::size_t i = 0; i < rgba.size(); i += 4)
          texels.push_back(static_cast<std::uint32_t>(rgba[i]) | (static_cast<std::uint32_t>(rgba[i+1]) << 8) |
            (static_cast<std::uint32_t>(rgba[i+2]) << 16) | (static_cast<std::uint32_t>(rgba[i+3]) << 24));
      }
      upload(8, texels);
      textureLayout = textures;
    } else if (!uploaded && textures.empty()) upload(8, std::vector<std::uint32_t>{});
    std::vector<GpuMaterial> materials;
    for (const auto &instance : instances) {
      const auto& region = instance.region;
      const auto& source = instance.texture;
      materials.push_back({pack(instance.material.baseColor, instance.material.roughness),
        pack(instance.material.emission, instance.material.twoSidedEmission ? 1.0f : 0.0f),
        {instance.material.ior, static_cast<float>(instance.material.model), instance.alphaCutoff, instance.unlit ? 1.0f : 0.0f},
        {region.x, region.y, region.width, region.height},
        {source ? textureOffsets.at(source.get()) : Invalid, source ? static_cast<std::uint32_t>(source->width()) : 0,
          source ? static_cast<std::uint32_t>(source->height()) : 0, 0}});
    }
    upload(4, materials);
    std::vector<GpuLight> lights;
    for(const auto& l:scene.lights()) lights.push_back({pack(l.position),pack(l.direction),pack(l.intensity,static_cast<float>(l.type)),
      {l.right.x,l.right.y,l.right.z,l.size.x},{l.up.x,l.up.y,l.up.z,l.size.y}});
    for(std::size_t i=0;i<lights.size();++i) {lights[i].position.w=scene.lights()[i].twoSided?1.f:0.f;}
    lightCount=indexOf(lights.size());upload(9,lights);
    checkGl();
    CheckCancellation(cancellation);
    layout = instances; previous = scene; uploaded = true;
  }
  void allocateTexture(GLuint target, ImageSize size, ImageSize& allocated)
  {
    glBindTexture(GL_TEXTURE_2D, target);
    if (allocated == size) return;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, size.width, size.height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    allocated = size;
  }
  bool dispatch(const RayScene& scene, const RenderRequest& request, GLuint output, bool progressive)
  {
    if (request.cancellation.stop_requested()) return false;
    (void)request.camera.generateRay({0.5f, 0.5f}, request.size);
    if (request.size.width > maximumTextureSize || request.size.height > maximumTextureSize)
      throw std::invalid_argument("Compute image exceeds the OpenGL texture size limit.");
    try { uploadScene(scene, request.cancellation); }
    catch (const BuildCancelled&) { return false; }
    if (request.cancellation.stop_requested()) return false;
    allocateTexture(accumulation, request.size, accumulationSize);
    const std::uint32_t zero = 0;
    glBindBuffer(ShaderStorageBuffer, buffers[7]);
    glBufferData(ShaderStorageBuffer, sizeof(zero), &zero, GL_STREAM_DRAW);
    glBindBufferBase(ShaderStorageBuffer, 7, buffers[7]);
    glUseProgram(program->Handle());
    const auto &u = uniforms; const auto &camera = request.camera;
    glUniform2i(u[0], request.size.width, request.size.height);
    glUniform3f(u[1], camera.position.x, camera.position.y, camera.position.z);
    glUniform3f(u[2], camera.right.x, camera.right.y, camera.right.z);
    glUniform3f(u[3], camera.up.x, camera.up.y, camera.up.z);
    glUniform3f(u[4], camera.forward.x, camera.forward.y, camera.forward.z);
    const bool ortho = camera.lens.projection == Projection::Orthographic;
    const float halfHeight = ortho ? camera.lens.viewHeight / 2 : std::tan(camera.lens.fovDegrees * Math::Pi / 360);
    glUniform4f(u[5], halfHeight, camera.lens.nearPlane, camera.lens.farPlane, ortho ? 1.0f : 0.0f);
    glUniform1ui(u[6], tlasRoot);
    glUniform1i(u[7], config.output == RayOutput::Normal);
    glUniform1i(u[8], config.jitter);
    glUniform2ui(u[9], static_cast<GLuint>(request.seed), static_cast<GLuint>(request.seed >> 32));
    glUniform2ui(u[10], static_cast<GLuint>(request.sampleIndex), static_cast<GLuint>(request.sampleIndex >> 32));
    glUniform1ui(u[11], emitterCount); glUniform1ui(u[12], config.maxBounces); glUniform1ui(u[13], config.rrStartBounce);
    glUniform1i(u[14], config.mode == RayMode::PathTrace); glUniform1i(u[15], progressive);
    glUniform1f(u[16], static_cast<float>(1.0 / (static_cast<double>(request.sampleIndex) + 1)));
    glUniform1ui(u[17],lightCount);
    device.bindImage(output); device.bindImage(accumulation, 1, GL_READ_WRITE);
    device.dispatch(static_cast<GLuint>((request.size.width + 7) / 8), static_cast<GLuint>((request.size.height + 7) / 8));
    device.readbackBarrier();
    checkGl();
    return true;
  }
  void validateSample()
  {
    std::uint32_t invalid = 0;
    glBindBuffer(ShaderStorageBuffer, buffers[7]);
    glGetBufferSubData(ShaderStorageBuffer, 0, sizeof(invalid), &invalid);
    checkGl();
    if (invalid) throw std::runtime_error("GPU path tracer produced invalid radiance.");
  }
};

ComputeRayTracer::ComputeRayTracer() : ComputeRayTracer([] {
  RayTracingSettings settings; settings.device = RayDevice::GPU; return settings;
}()) {}
ComputeRayTracer::ComputeRayTracer(RayTracingSettings config) : ComputeRayTracer(config, false) {}
ComputeRayTracer::ComputeRayTracer(RayTracingSettings config, bool shared)
{
  Detail::validateRayTracingSettings(config);
  if (config.device != RayDevice::GPU) throw std::invalid_argument("ComputeRayTracer requires GPU execution.");
  m_Impl = std::make_unique<Impl>(config, shared);
}
ComputeRayTracer::~ComputeRayTracer() = default;
ProgressiveSettings ComputeRayTracer::samplingSettings() const { return m_Impl->config; }
ImageEncoding ComputeRayTracer::imageEncoding() const noexcept {
  return m_Impl->config.output == RayOutput::Normal ? ImageEncoding::LinearData : ImageEncoding::Radiance;
}
void ComputeRayTracer::configure(const RayTracingSettings& settings)
{
  Detail::validateRayTracingSettings(settings);
  if (settings.device != RayDevice::GPU) throw std::invalid_argument("Compute requires GPU execution.");
  std::lock_guard lock(m_Impl->renderMutex);
  m_Impl->config = settings;
}
std::string_view ComputeRayTracer::deviceName() const noexcept { return m_Impl->device.name(); }
std::string_view ComputeRayTracer::name() const noexcept { return m_Impl->config.mode == RayMode::PathTrace ? "GPU Path Tracing" : "GPU Ray Casting"; }
bool ComputeRayTracer::hasGpuFrames() const noexcept { return m_Impl->shared; }
std::shared_ptr<GpuFrame> ComputeRayTracer::renderGpuFrame(const RayScene& scene, const RenderRequest& request)
{
  std::unique_lock lock(m_Impl->renderMutex, std::try_to_lock);
  if (!lock.owns_lock()) throw std::logic_error("Concurrent rendering on the same compute backend is unsupported.");
  if (request.cancellation.stop_requested()) return {};
  auto& gpu = *m_Impl;
  ComputeContextScope context(gpu.device);
  (void)request.camera.generateRay({0.5f, 0.5f}, request.size);
  if (request.size.width > gpu.maximumTextureSize || request.size.height > gpu.maximumTextureSize)
    throw std::invalid_argument("Compute image exceeds the OpenGL texture size limit.");
  std::shared_ptr<GpuFrame> frame;
  for (const auto& slot : gpu.frames) if (slot.use_count() == 1) { frame = slot; break; }
  if (!frame) throw std::logic_error("All GPU presentation slots are retained.");
  if (const auto consumed = frame->consumed.exchange(nullptr)) { glWaitSync(consumed, 0, GL_TIMEOUT_IGNORED); glDeleteSync(consumed); }
  if (frame->ready) { glDeleteSync(frame->ready); frame->ready = nullptr; }
  gpu.allocateTexture(frame->texture, request.size, frame->size);
  if (!gpu.dispatch(scene, request, frame->texture, true)) return {};
  frame->ready = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  if (!frame->ready) throw std::runtime_error("Cannot fence GPU frame completion.");
  glFlush();
  for (;;) {
    if (request.cancellation.stop_requested()) return {};
    const auto status = glClientWaitSync(frame->ready, 0, 1000000);
    if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) break;
    if (status == GL_WAIT_FAILED) throw std::runtime_error("GPU frame completion wait failed.");
  }
  gpu.validateSample();
  return frame;
}
void ComputeRayTracer::render(const RayScene &scene, const RenderRequest &request, Film &film)
{
  std::unique_lock lock(m_Impl->renderMutex, std::try_to_lock);
  if (!lock.owns_lock()) throw std::logic_error("Concurrent rendering on the same compute backend is unsupported.");
  if (request.size != film.size() || request.sampleIndex != film.samples())
    throw std::invalid_argument("RenderRequest size/sample index must match Film.");
  if (request.cancellation.stop_requested()) return;
  auto& gpu = *m_Impl;
  ComputeContextScope context(gpu.device);
  // Validate dimensions before allocating any GPU image.
  (void)request.camera.generateRay({0.5f, 0.5f}, request.size);
  if (request.size.width > gpu.maximumTextureSize || request.size.height > gpu.maximumTextureSize)
    throw std::invalid_argument("Compute image exceeds the OpenGL texture size limit.");
  gpu.allocateTexture(gpu.texture, request.size, gpu.textureSize);
  if (!gpu.dispatch(scene, request, gpu.texture, false)) return;
  // Explicit headless/export contract only. Runtime uses renderGpuFrame instead.
  std::vector<Float4> rgba(request.size.pixels());
  glBindTexture(GL_TEXTURE_2D, gpu.texture);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, rgba.data());
  gpu.validateSample();
  if (request.cancellation.stop_requested()) return;
  std::vector<LinearRGB> sample; sample.reserve(rgba.size());
  for (const auto& pixel : rgba) sample.push_back({pixel.x, pixel.y, pixel.z});
  if (!request.cancellation.stop_requested()) film.addSample(sample);
}
} // namespace Jade::Renderer
