#pragma once
#include <Jade/Graphics/Geometry.hpp>
#include <Jade/Renderer/RayTracing/Ray.hpp>
#include "Math.hpp"
#include <numbers>
namespace Jade::Renderer {
inline bool intersectPrimitive(GeometryKind kind, const Ray& ray, SurfaceHit& hit) {
  double t;
  if (kind == GeometryKind::Sphere) {
    const double a = static_cast<double>(ray.direction.x)*ray.direction.x + static_cast<double>(ray.direction.y)*ray.direction.y + static_cast<double>(ray.direction.z)*ray.direction.z;
    const double b = static_cast<double>(ray.origin.x)*ray.direction.x + static_cast<double>(ray.origin.y)*ray.direction.y + static_cast<double>(ray.origin.z)*ray.direction.z;
    const double c = static_cast<double>(ray.origin.x)*ray.origin.x + static_cast<double>(ray.origin.y)*ray.origin.y + static_cast<double>(ray.origin.z)*ray.origin.z - 1;
    const auto discriminant = b*b-a*c;
    if (a == 0 || discriminant < 0) return false;
    const auto q = -b-std::copysign(std::sqrt(discriminant), b);
    auto near = q == 0 ? -b/a : q/a, far = q == 0 ? near : c/q;
    if (near > far) std::swap(near, far);
    t = near > ray.tMin ? near : far;
    if (t <= ray.tMin || t > ray.tMax) return false;
    hit.position = ray.at(static_cast<float>(t));
    hit.geometricNormal = Math::unit(hit.position);
    hit.uv = {std::atan2(hit.position.z,hit.position.x)/(2*std::numbers::pi_v<float>) + .5f,
              std::acos(std::clamp(hit.geometricNormal.y,-1.0f,1.0f))/std::numbers::pi_v<float>};
  } else {
    if (ray.direction.y == 0) return false;
    t = -static_cast<double>(ray.origin.y)/ray.direction.y;
    if (t <= ray.tMin || t > ray.tMax) return false;
    hit.position = ray.at(static_cast<float>(t));
    if (std::abs(hit.position.x) > .5f || std::abs(hit.position.z) > .5f) return false;
    hit.geometricNormal = {0,1,0}; hit.uv = {hit.position.x+.5f,hit.position.z+.5f};
  }
  hit.t = static_cast<float>(t); hit.shadingNormal = hit.geometricNormal; hit.primitive = 0;
  hit.positionError = Math::abs(hit.position) * (16*std::numeric_limits<float>::epsilon());
  return true;
}
inline float determinant(const Matrix4& m) {
  return dot(Math::vector(m,{1,0,0}),cross(Math::vector(m,{0,1,0}),Math::vector(m,{0,0,1})));
}
inline float areaJacobian(const Matrix4& model, const Matrix4& inverse, Vec3 normal) {
  Vec3 transformed{
    inverse.at(0,0)*normal.x+inverse.at(1,0)*normal.y+inverse.at(2,0)*normal.z,
    inverse.at(0,1)*normal.x+inverse.at(1,1)*normal.y+inverse.at(2,1)*normal.z,
    inverse.at(0,2)*normal.x+inverse.at(1,2)*normal.y+inverse.at(2,2)*normal.z};
  return std::abs(determinant(model))*Math::length(transformed);
}
}
