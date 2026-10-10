#include "BSDF.hpp"
#include "../Common/Math.hpp"

#include <stdexcept>

namespace Jade::Renderer {
namespace {

float fresnel(float cosine, float eta)
{
  cosine = std::clamp(std::abs(cosine), 0.0f, 1.0f);
  const float sinSquared = (1 - cosine * cosine) / (eta * eta);
  if (sinSquared >= 1)
    return 1;
  const float transmitted = std::sqrt(std::max(0.0f, 1 - sinSquared));
  const float parallel = (eta * cosine - transmitted) / (eta * cosine + transmitted);
  const float perpendicular = (cosine - eta * transmitted) / (cosine + eta * transmitted);
  return (parallel * parallel + perpendicular * perpendicular) / 2;
}
LinearRGB schlick(LinearRGB f0, float cosine)
{
  const float factor = std::pow(std::clamp(1 - std::abs(cosine), 0.0f, 1.0f), 5.0f);
  return f0 * (1 - factor) + LinearRGB{factor, factor, factor};
}
float alpha(const Material &material) { return std::max(0.001f, material.roughness * material.roughness); }
float distribution(Vec3 half, float a)
{
  if (half.z <= 0)
    return 0;
  const double aa = static_cast<double>(a) * a;
  const double denominator = static_cast<double>(half.z) * half.z * (aa - 1) + 1;
  return static_cast<float>(aa / (Math::Pi * denominator * denominator));
}
float masking(Vec3 v, float a)
{
  if (v.z == 0)
    return 0;
  const double tangent =
      (static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y) / (static_cast<double>(v.z) * v.z);
  return static_cast<float>(2 / (1 + std::sqrt(1 + static_cast<double>(a) * a * tangent)));
}
float visiblePdf(Vec3 outgoing, Vec3 half, float a)
{
  return outgoing.z > 0
             ? distribution(half, a) * masking(outgoing, a) * std::abs(dot(outgoing, half)) / outgoing.z
             : 0;
}
Vec3 visibleNormal(Vec3 outgoing, float a, Vec2 sample)
{
  const auto stretched = Math::unit({a * outgoing.x, a * outgoing.y, outgoing.z});
  const float radial = std::hypot(stretched.x, stretched.y);
  const Vec3 tangent = radial > 0 ? Vec3{-stretched.y / radial, stretched.x / radial, 0} : Vec3{1, 0, 0};
  const auto bitangent = cross(stretched, tangent);
  const float radius = std::sqrt(sample.x), phi = 2 * Math::Pi * sample.y;
  const float x = radius * std::cos(phi);
  float y = radius * std::sin(phi);
  const float blend = (1 + stretched.z) / 2;
  y = (1 - blend) * std::sqrt(std::max(0.0f, 1 - x * x)) + blend * y;
  const auto normal = tangent * x + bitangent * y + stretched * std::sqrt(std::max(0.0f, 1 - x * x - y * y));
  return Math::unit({a * normal.x, a * normal.y, std::max(0.0f, normal.z)});
}
Vec3 refract(Vec3 outgoing, Vec3 normal, float eta)
{
  const float cosine = std::clamp(dot(outgoing, normal), 0.0f, 1.0f);
  const float sinSquared = (1 - cosine * cosine) / (eta * eta);
  if (sinSquared >= 1)
    return {};
  return -outgoing * (1 / eta) + normal * (cosine / eta - std::sqrt(std::max(0.0f, 1 - sinSquared)));
}

} // namespace

BSDF::BSDF(const Material &material, const SurfaceHit &hit)
    : m_Material(material), m_Geometric(hit.frontFace ? hit.geometricNormal : -hit.geometricNormal),
      m_FrontFace(hit.frontFace)
{
  validateMaterial(material);
  m_Normal = material.model == MaterialModel::Dielectric ? m_Geometric
             : hit.frontFace                             ? hit.shadingNormal
                                                         : -hit.shadingNormal;
  m_Normal = Math::unit(m_Normal);
  m_Geometric = Math::unit(m_Geometric);
  Math::basis(m_Normal, m_Tangent, m_Bitangent);
}
Vec3 BSDF::local(Vec3 direction) const
{
  return {dot(direction, m_Tangent), dot(direction, m_Bitangent), dot(direction, m_Normal)};
}
Vec3 BSDF::world(Vec3 direction) const
{
  return m_Tangent * direction.x + m_Bitangent * direction.y + m_Normal * direction.z;
}
bool BSDF::delta() const noexcept
{
  return (m_Material.model != MaterialModel::Diffuse && m_Material.roughness == 0) ||
         (m_Material.model == MaterialModel::Dielectric && m_Material.ior == 1);
}

LinearRGB BSDF::evaluate(Vec3 outgoing, Vec3 incoming) const
{
  const auto wo = local(outgoing), wi = local(incoming);
  if (wo.z <= 0 || wi.z == 0 || delta())
    return {};
  const bool reflection = wi.z > 0;
  const float geometricProduct = dot(outgoing, m_Geometric) * dot(incoming, m_Geometric);
  if ((reflection && geometricProduct <= 0) || (!reflection && geometricProduct >= 0))
    return {};
  if (m_Material.model == MaterialModel::Diffuse)
    return reflection ? m_Material.baseColor * (1 / Math::Pi) : LinearRGB{};
  if (m_Material.model == MaterialModel::Metal && !reflection)
    return {};
  const float eta = m_FrontFace ? m_Material.ior : 1 / m_Material.ior;
  const auto halfVector = reflection ? wo + wi : wo + wi * eta;
  if (Math::length(halfVector) == 0)
    return {};
  auto half = Math::unit(halfVector);
  if (half.z < 0)
    half = -half;
  const float cosO = dot(wo, half), cosI = dot(wi, half);
  if (cosO * wo.z <= 0 || cosI * wi.z <= 0)
    return {};
  const float a = alpha(m_Material);
  const float common = distribution(half, a) * masking(wo, a) * masking(wi, a);
  if (m_Material.model == MaterialModel::Metal)
    return schlick(m_Material.baseColor, cosO) * (common / (4 * wo.z * wi.z));
  const float F = fresnel(cosO, eta);
  if (reflection)
    return LinearRGB{1, 1, 1} * (F * common / (4 * wo.z * wi.z));
  const float denominator = cosI + cosO / eta;
  if (denominator == 0)
    return {};
  const float value =
      (1 - F) * common * std::abs(cosI * cosO / (wi.z * wo.z * denominator * denominator)) / (eta * eta);
  return m_Material.baseColor * value;
}

float BSDF::pdf(Vec3 outgoing, Vec3 incoming) const
{
  const auto wo = local(outgoing), wi = local(incoming);
  if (wo.z <= 0 || wi.z == 0 || delta())
    return 0;
  const bool reflection = wi.z > 0;
  const float geometricProduct = dot(outgoing, m_Geometric) * dot(incoming, m_Geometric);
  if ((reflection && geometricProduct <= 0) || (!reflection && geometricProduct >= 0))
    return 0;
  if (m_Material.model == MaterialModel::Diffuse)
    return reflection ? wi.z / Math::Pi : 0;
  if (m_Material.model == MaterialModel::Metal && !reflection)
    return 0;
  const float eta = m_FrontFace ? m_Material.ior : 1 / m_Material.ior;
  const auto halfVector = reflection ? wo + wi : wo + wi * eta;
  if (Math::length(halfVector) == 0)
    return 0;
  auto half = Math::unit(halfVector);
  if (half.z < 0)
    half = -half;
  const float cosO = dot(wo, half), cosI = dot(wi, half);
  if (cosO * wo.z <= 0 || cosI * wi.z <= 0)
    return 0;
  const float halfPdf = visiblePdf(wo, half, alpha(m_Material));
  if (m_Material.model == MaterialModel::Metal)
    return halfPdf / (4 * std::abs(cosO));
  const float F = fresnel(cosO, eta);
  if (reflection)
    return F * halfPdf / (4 * std::abs(cosO));
  const float denominator = cosI + cosO / eta;
  return denominator != 0 ? (1 - F) * halfPdf * std::abs(cosI) / (denominator * denominator) : 0;
}

BSDFSample BSDF::sample(Vec3 outgoing, Sampler &sampler) const
{
  const auto wo = local(outgoing);
  if (wo.z <= 0)
    return {};
  Vec3 incoming;
  bool transmission = false;
  const float eta = m_FrontFace ? m_Material.ior : 1 / m_Material.ior;
  if (m_Material.model == MaterialModel::Diffuse) {
    const auto u = sampler.next2D();
    const float radius = std::sqrt(u.x), angle = 2 * Math::Pi * u.y;
    incoming = {radius * std::cos(angle), radius * std::sin(angle), std::sqrt(std::max(0.0f, 1 - u.x))};
  } else {
    const auto half = delta() ? Vec3{0, 0, 1} : visibleNormal(wo, alpha(m_Material), sampler.next2D());
    const float F = m_Material.model == MaterialModel::Dielectric ? fresnel(dot(wo, half), eta) : 1;
    transmission = m_Material.model == MaterialModel::Dielectric && sampler.next1D() >= F;
    incoming = transmission ? refract(wo, half, eta) : -wo + half * (2 * dot(wo, half));
    if (Math::length(incoming) == 0)
      return {};
    const auto direction = Math::unit(world(incoming));
    if (delta()) {
      const float sides = dot(outgoing, m_Geometric) * dot(direction, m_Geometric);
      if ((transmission && sides >= 0) || (!transmission && sides <= 0))
        return {};
      const float cosine = std::abs(dot(direction, m_Normal));
      if (cosine == 0)
        return {};
      if (m_Material.model == MaterialModel::Metal)
        return {direction, schlick(m_Material.baseColor, wo.z) / cosine, 1, true, false, 1};
      return {direction,
              transmission ? m_Material.baseColor * ((1 - F) / (cosine * eta * eta))
                           : LinearRGB{1, 1, 1} * (F / cosine),
              transmission ? 1 - F : F,
              true,
              transmission,
              transmission ? eta : 1};
    }
  }
  const auto direction = Math::unit(world(incoming));
  return {direction,    evaluate(outgoing, direction), pdf(outgoing, direction), false,
          transmission, transmission ? eta : 1};
}

} // namespace Jade::Renderer
