#include "ContactSolver.hpp"
#include <emmintrin.h>
#include <intrin.h>

namespace Jade::Physics::Internal {
namespace {
bool avx2Available() {
  int info[4]; __cpuid(info,1);
  if (!(info[2]&(1<<27)) || !(info[2]&(1<<28)) || (_xgetbv(0)&6) != 6) return false;
  __cpuidex(info,7,0); return (info[1]&(1<<5)) != 0;
}
}
SolverBackend selectBackend(SolverBackend requested) {
  static const bool avx2 = avx2Available();
  if (requested == SolverBackend::Automatic) return avx2 ? SolverBackend::AVX2 : SolverBackend::SSE2;
  if (requested == SolverBackend::AVX2 && !avx2) throw std::invalid_argument("AVX2 solver requested on an unsupported CPU.");
  if (requested != SolverBackend::Scalar && requested != SolverBackend::SSE2 && requested != SolverBackend::AVX2) throw std::invalid_argument("Unknown solver backend.");
  return requested;
}
void solveVelocityPacket(std::span<Body> bodies,std::array<Constraint*,4> packet,SolverBackend backend) {
  alignas(32) double old[4]{},target[4]{},speed[4]{},mass[4]{},result[4]{};
  for (unsigned i = 0; i < 4; ++i) if (packet[i]) {
    const auto& c = *packet[i]; old[i] = c.normalImpulse; target[i] = c.target; mass[i] = c.normalMass;
    speed[i] = collisionDot(relative(bodies[c.a],bodies[c.b],c.ra,c.rb),c.normal);
  }
  if (backend == SolverBackend::AVX2) normalFourAvx2(old,target,speed,mass,result);
  else if (backend == SolverBackend::SSE2) for (unsigned i = 0; i < 4; i += 2) {
    const auto next = _mm_add_pd(_mm_load_pd(old+i),_mm_mul_pd(_mm_sub_pd(_mm_load_pd(target+i),_mm_load_pd(speed+i)),_mm_load_pd(mass+i)));
    _mm_store_pd(result+i,_mm_max_pd(_mm_setzero_pd(),next));
  } else for (unsigned i = 0; i < 4; ++i) result[i] = std::max(0.0,old[i]+(target[i]-speed[i])*mass[i]);
  for (unsigned i = 0; i < 4; ++i) if (packet[i]) {
    auto& c = *packet[i]; auto& a = bodies[c.a]; auto& b = bodies[c.b];
    c.normalImpulse = result[i]; applyVelocityImpulse(bodies,c,result[i]-old[i],0,0);
    const auto rv = relative(a,b,c.ra,c.rb); const auto u = collisionDot(rv,c.tangent1),v = collisionDot(rv,c.tangent2);
    double next1 = c.tangentImpulse1-c.tangentMass11*u-c.tangentMass12*v;
    double next2 = c.tangentImpulse2-c.tangentMass12*u-c.tangentMass22*v;
    const double limit = c.friction*c.normalImpulse,length = std::hypot(next1,next2);
    if (length > limit && length > 0) { next1 *= limit/length; next2 *= limit/length; }
    applyVelocityImpulse(bodies,c,0,next1-c.tangentImpulse1,next2-c.tangentImpulse2);
    c.tangentImpulse1 = next1; c.tangentImpulse2 = next2;
  }
}
} // namespace Jade::Physics::Internal
