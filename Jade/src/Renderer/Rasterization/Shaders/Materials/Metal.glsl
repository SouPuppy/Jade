#ifndef JADE_RASTER_MATERIALS_METAL_GLSL
#define JADE_RASTER_MATERIALS_METAL_GLSL
vec3 fresnel(vec3 f0, float c) { return mix(f0, vec3(1), pow(clamp(1 - c, 0, 1), 5)); }
#endif
