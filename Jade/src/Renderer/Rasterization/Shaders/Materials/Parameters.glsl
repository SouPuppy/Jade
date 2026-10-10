#ifndef JADE_RASTER_MATERIALS_PARAMETERS_GLSL
#define JADE_RASTER_MATERIALS_PARAMETERS_GLSL
const int MATERIAL_LAMBERT = 0;
const int MATERIAL_CONDUCTOR = 1;
const int MATERIAL_DIELECTRIC = 2;
uniform vec3 u_BaseColor, u_Emission;
uniform int u_ModelKind;
uniform float u_Roughness, u_Ior;
uniform bool u_TwoSided;
vec3 emitted() { return gl_FrontFacing || u_TwoSided ? u_Emission : vec3(0); }
#endif
