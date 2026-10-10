#ifndef JADE_RASTER_COMMON_SAMPLING_GLSL
#define JADE_RASTER_COMMON_SAMPLING_GLSL
float radical(uint x)
{
  x = (x << 16) | (x >> 16);
  x = ((x & 0x55555555u) << 1) | ((x & 0xaaaaaaaau) >> 1);
  x = ((x & 0x33333333u) << 2) | ((x & 0xccccccccu) >> 2);
  x = ((x & 0x0f0f0f0fu) << 4) | ((x & 0xf0f0f0f0u) >> 4);
  x = ((x & 0x00ff00ffu) << 8) | ((x & 0xff00ff00u) >> 8);
  return float(x) * 2.3283064365386963e-10;
}
#endif
