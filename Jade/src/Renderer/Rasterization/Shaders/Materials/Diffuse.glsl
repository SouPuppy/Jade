#ifndef JADE_RASTER_MATERIALS_DIFFUSE_GLSL
#define JADE_RASTER_MATERIALS_DIFFUSE_GLSL
#include "../Common/Constants.glsl"
vec3 lambert(vec3 reflectance) { return reflectance / PI; }
#endif
