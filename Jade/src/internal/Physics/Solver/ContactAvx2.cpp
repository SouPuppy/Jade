#include <immintrin.h>
namespace Jade::Physics::Internal {
void normalFourAvx2(const double* old,const double* target,const double* speed,const double* mass,double* result) {
  const auto next = _mm256_add_pd(_mm256_load_pd(old),_mm256_mul_pd(_mm256_sub_pd(_mm256_load_pd(target),_mm256_load_pd(speed)),_mm256_load_pd(mass)));
  _mm256_store_pd(result,_mm256_max_pd(_mm256_setzero_pd(),next));
}
}
