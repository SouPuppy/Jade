#ifndef JADE_RASTER_MATERIALS_DIELECTRIC_GLSL
#define JADE_RASTER_MATERIALS_DIELECTRIC_GLSL
float dielectric(float c, float eta)
{
  if (abs(eta - 1) < 1e-6)
    return 0.0;
  c = clamp(abs(c), 0, 1);
  float sin2 = (1 - c * c) / (eta * eta);
  if (sin2 >= 1)
    return 1.0;
  float ct = sqrt(1 - sin2);
  float a = (eta * c - ct) / (eta * c + ct), b = (c - eta * ct) / (c + eta * ct);
  return .5 * (a * a + b * b);
}
#endif
