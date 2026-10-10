#include "OpenGLPrimitiveRenderer.hpp"

#include <array>
#include <stdexcept>

namespace Jade::Detail {
namespace {

constexpr const char* vertexSource = R"(#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
uniform mat4 u_Transform;
out vec4 vertexColor;
void main()
{
  gl_Position = u_Transform * vec4(a_Position, 1.0);
  vertexColor = a_Color;
}
)";

constexpr const char* fragmentSource = R"(#version 330 core
in vec4 vertexColor;
out vec4 fragmentColor;
void main()
{
  fragmentColor = vertexColor;
}
)";

void WriteVertex(float* destination, Vec3 position, Color color)
{
  destination[0] = position.x;
  destination[1] = position.y;
  destination[2] = position.z;
  destination[3] = color.red;
  destination[4] = color.green;
  destination[5] = color.blue;
  destination[6] = color.alpha;
}

Matrix4 OverlayTransform(FramebufferSize size, float viewHeight)
{
  return OverlayMatrix(static_cast<double>(viewHeight) * size.width / size.height, viewHeight);
}

} // namespace

OpenGLPrimitiveRenderer::OpenGLPrimitiveRenderer() : m_Program(vertexSource, fragmentSource)
{
  try {
    m_TransformLocation = m_Program.Uniform("u_Transform");
    glGenVertexArrays(1, &m_VertexArray);
    glGenBuffers(1, &m_VertexBuffer);
    if (m_VertexArray == 0 || m_VertexBuffer == 0) {
      throw std::runtime_error("Failed to allocate primitive vertex objects.");
    }
    glBindVertexArray(m_VertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
      reinterpret_cast<const void*>(3 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  } catch (...) {
    Release();
    throw;
  }
}

OpenGLPrimitiveRenderer::~OpenGLPrimitiveRenderer()
{
  Release();
}

void OpenGLPrimitiveRenderer::Release() noexcept
{
  if (m_VertexBuffer != 0) {
    glDeleteBuffers(1, &m_VertexBuffer);
  }
  if (m_VertexArray != 0) {
    glDeleteVertexArrays(1, &m_VertexArray);
  }
}

void OpenGLPrimitiveRenderer::Draw(const float* vertices, GLsizei count,
  GLenum topology, const Matrix4& transform)
{
  glUseProgram(m_Program.Handle());
  glUniformMatrix4fv(m_TransformLocation, 1, GL_FALSE, transform.data());
  glBindVertexArray(m_VertexArray);
  glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count) * 7 * sizeof(float), vertices, GL_STREAM_DRAW);
  glDrawArrays(topology, 0, count);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
}

void OpenGLPrimitiveRenderer::DrawLine(const Line& line, FramebufferSize size, float viewHeight)
{
  if (size.width <= 0 || size.height <= 0 || line.Start() == line.End()) {
    return;
  }
  std::array<float, 14> vertices{};
  WriteVertex(vertices.data(), {line.Start().x, line.Start().y, 0}, {1, 1, 1, 1});
  WriteVertex(vertices.data() + 7, {line.End().x, line.End().y, 0}, {1, 1, 1, 1});
  glDisable(GL_DEPTH_TEST);
  glLineWidth(1.0f);
  Draw(vertices.data(), 2, GL_LINES, OverlayTransform(size, viewHeight));
}

void OpenGLPrimitiveRenderer::DrawTriangle(const Triangle& triangle, FramebufferSize size, float viewHeight)
{
  if (size.width <= 0 || size.height <= 0) {
    return;
  }
  const auto& points = triangle.Vertices();
  if (cross(points[1] - points[0], points[2] - points[0]) == 0.0f) {
    return;
  }
  std::array<float, 21> vertices{};
  for (std::size_t index = 0; index < points.size(); ++index) {
    WriteVertex(vertices.data() + index * 7, {points[index].x, points[index].y, 0}, triangle.GetColor());
  }
  glDisable(GL_DEPTH_TEST);
  Draw(vertices.data(), 3, GL_TRIANGLES, OverlayTransform(size, viewHeight));
}

void OpenGLPrimitiveRenderer::DrawAxisGizmo(const Camera& camera, FramebufferSize size)
{
  if (size.width <= 0 || size.height <= 0) {
    return;
  }
  // Three world-space segments, each exactly one unit long. No object transform.
  constexpr std::array<Vec3, 3> endpoints{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  constexpr std::array<Color, 3> colors{{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}}};
  std::array<float, 6 * 7> vertices{};
  for (std::size_t axis = 0; axis < endpoints.size(); ++axis) {
    WriteVertex(vertices.data() + axis * 14, {0, 0, 0}, colors[axis]);
    WriteVertex(vertices.data() + axis * 14 + 7, endpoints[axis], colors[axis]);
  }
  const auto transform = ProjectionMatrix(camera, static_cast<double>(size.width) / size.height)
    * ViewMatrix(camera);
  const auto depthTest = glIsEnabled(GL_DEPTH_TEST);
  GLboolean depthWrite = GL_TRUE;
  glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
  // Draw over world objects so the origin axes remain visible inside world meshes.
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  Draw(vertices.data(), 6, GL_LINES, transform);
  glDepthMask(depthWrite);
  if (depthTest == GL_TRUE) {
    glEnable(GL_DEPTH_TEST);
  }
}

} // namespace Jade::Detail
