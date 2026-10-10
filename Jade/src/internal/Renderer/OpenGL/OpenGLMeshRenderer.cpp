#include "OpenGLMeshRenderer.hpp"

#include "../../Math/Matrix4.hpp"

#include <stdexcept>

namespace Jade::Detail {
namespace {

constexpr const char* vertexSource = R"(#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
uniform mat4 u_Transform;
uniform mat4 u_NormalTransform;
out vec3 worldNormal;
void main()
{
  gl_Position = u_Transform * vec4(a_Position, 1.0);
  worldNormal = (u_NormalTransform * vec4(a_Normal, 0.0)).xyz;
}
)";

constexpr const char* fragmentSource = R"(#version 330 core
in vec3 worldNormal;
uniform vec4 u_Color;
out vec4 fragmentColor;
void main()
{
  vec3 lightDirection = normalize(vec3(0.3, 0.7, 1.0));
  float diffuse = dot(worldNormal, worldNormal) > 0.0
    ? max(dot(normalize(worldNormal), lightDirection), 0.0) : 0.0;
  fragmentColor = vec4(u_Color.rgb * (0.25 + 0.75 * diffuse), u_Color.a);
}
)";

} // namespace

OpenGLMeshRenderer::OpenGLMeshRenderer() : m_Program(vertexSource, fragmentSource)
{
  m_TransformLocation = m_Program.Uniform("u_Transform");
  m_NormalLocation = m_Program.Uniform("u_NormalTransform");
  m_ColorLocation = m_Program.Uniform("u_Color");
  try {
    glGenVertexArrays(1, &m_VertexArray);
    glGenBuffers(1, &m_VertexBuffer);
    glGenBuffers(1, &m_IndexBuffer);
    if (m_VertexArray == 0 || m_VertexBuffer == 0 || m_IndexBuffer == 0) {
      throw std::runtime_error("Failed to allocate mesh vertex/index objects.");
    }
    glBindVertexArray(m_VertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IndexBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
      reinterpret_cast<const void*>(3 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  } catch (...) {
    Release();
    throw;
  }
}

OpenGLMeshRenderer::~OpenGLMeshRenderer()
{
  Release();
}

void OpenGLMeshRenderer::Release() noexcept
{
  if (m_IndexBuffer != 0) {
    glDeleteBuffers(1, &m_IndexBuffer);
  }
  if (m_VertexBuffer != 0) {
    glDeleteBuffers(1, &m_VertexBuffer);
  }
  if (m_VertexArray != 0) {
    glDeleteVertexArrays(1, &m_VertexArray);
  }
}

void OpenGLMeshRenderer::DrawObject(const Object& object, const Camera& camera, FramebufferSize size)
{
  const auto& mesh = object.GetMesh();
  if (size.width <= 0 || size.height <= 0 || mesh.indices.empty()) {
    return;
  }
  const auto transform = ProjectionMatrix(camera, static_cast<double>(size.width) / size.height)
    * ViewMatrix(camera) * ModelMatrix(object);
  const auto normalTransform = NormalMatrix(object);
  m_VertexData.clear();
  m_VertexData.reserve(mesh.vertices.size() * 6);
  for (const auto& vertex : mesh.vertices) {
    m_VertexData.insert(m_VertexData.end(), {vertex.position.x, vertex.position.y, vertex.position.z,
      vertex.normal.x, vertex.normal.y, vertex.normal.z});
  }
  const auto color = object.GetColor();
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glUseProgram(m_Program.Handle());
  glUniformMatrix4fv(m_TransformLocation, 1, GL_FALSE, transform.data());
  glUniformMatrix4fv(m_NormalLocation, 1, GL_FALSE, normalTransform.data());
  glUniform4f(m_ColorLocation, color.red, color.green, color.blue, color.alpha);
  glBindVertexArray(m_VertexArray);
  glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_VertexData.size() * sizeof(float)),
    m_VertexData.data(), GL_STREAM_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IndexBuffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
    mesh.indices.data(), GL_STREAM_DRAW);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
}

} // namespace Jade::Detail
