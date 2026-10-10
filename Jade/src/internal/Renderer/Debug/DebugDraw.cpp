#include "DebugDraw.hpp"
#include <JadeShaders.hpp>
#include <array>
namespace Jade::Detail {
DebugDraw::DebugDraw(OpenGLDevice &device) : m_Device(device)
{
  device.current();
  m_LineProgram = std::make_unique<OpenGLProgram>(Shaders::lines_vert, Shaders::lines_frag);
  glGenVertexArrays(1, &m_Vao);
  glGenBuffers(1, &m_Buffer);
  glBindVertexArray(m_Vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_Buffer);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), nullptr);
}
DebugDraw::~DebugDraw()
{
  m_Device.current();
  m_LineProgram.reset();
  glDeleteBuffers(1, &m_Buffer);
  glDeleteVertexArrays(1, &m_Vao);
}
void DebugDraw::lines(std::span<const Vec3> points, const Matrix4 &transform, Vec3 color, bool depth)
{
  if (points.empty())
    return;
  glUseProgram(m_LineProgram->Handle());
  glBindVertexArray(m_Vao);
  glBindBuffer(GL_ARRAY_BUFFER, m_Buffer);
  glUniformMatrix4fv(m_LineProgram->Uniform("u_Transform"), 1, GL_FALSE, transform.data());
  glUniform3f(m_LineProgram->Uniform("u_Color"), color.x, color.y, color.z);
  if (depth)
    glEnable(GL_DEPTH_TEST);
  else
    glDisable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(depth ? GL_TRUE : GL_FALSE);
  // Bound each upload/draw independently of the CPU mesh's total index count.
  constexpr std::size_t batch = 65536;
  for (std::size_t offset = 0; offset < points.size(); offset += batch) {
    const auto count = std::min(batch, points.size() - offset);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * sizeof(Vec3)), points.data() + offset,
                 GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(count));
  }
  glDepthMask(GL_TRUE);
  glDisable(GL_DEPTH_TEST);
}
void DebugDraw::wireframe(const Renderer::SceneSnapshot &snapshot, Renderer::ImageSize size)
{
  if (!snapshot.camera)
    return;
  const auto projection = snapshot.camera->viewProjection(size);
  std::vector<Vec3> edges;
  for (const auto &instance : snapshot.instances) {
    const auto &mesh = instance.mesh->data();
    edges.clear();
    edges.reserve(mesh.indices.size() * 2);
    for (std::size_t index = 0; index < mesh.indices.size(); index += 3) {
      const Vec3 a = mesh.vertices[mesh.indices[index]].position,
                 b = mesh.vertices[mesh.indices[index + 1]].position,
                 c = mesh.vertices[mesh.indices[index + 2]].position;
      edges.insert(edges.end(), {a, b, b, c, c, a});
    }
    lines(edges, projection * instance.objectToWorld, {0.8f, 0.85f, 0.9f}, true);
  }
}
void DebugDraw::axes(const Renderer::CameraFrame &camera, Renderer::ImageSize size)
{
  const auto projection = camera.viewProjection(size);
  const std::array<Vec3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  for (const auto axis : axes) {
    const std::array<Vec3, 2> points{{{}, axis}};
    lines(points, projection, axis, false);
  }
}
} // namespace Jade::Detail
